#!/usr/bin/env python3
"""Synthetic-only M28.2 checks. Never invokes a privileged acquisition."""
import hashlib
import json
import os
import pathlib
import shutil
import socket
import subprocess
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "qualification/m28-2-readonly-acquisition.sh"
TARGET = "/var/lib/athena-alpha-m1"
EXPECTED = ["/usr/local/sbin/athena-alpha-m1-runner", "/usr/local/sbin/gateway_support.py",
 "/etc/sudoers.d/athena-alpha-m1", "/var/lib/athena-alpha-m1/admin-install.lock",
 "/var/lib/athena-alpha-m1", "/var/lib/athena-alpha-m1/backups", "/root/athena-alpha-m1-stage",
 "/run/athena-alpha-m1", "/var/log/athena-alpha-m1", "/var/lib/athena-alpha-m1/systemd-canary.json",
 "/var/lib/athena-alpha-m1/ext4-fixture", "/var/tmp/athena-d67-ext4", "/opt/athena-qualification/build"]


@unittest.skipIf(os.geteuid() == 0, "synthetic acquisition is intentionally forbidden as root")
class AcquisitionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="m28-2-")
        os.chmod(self.tmp, 0o700)
        self.root = pathlib.Path(self.tmp) / "fixture"
        self.root.mkdir(mode=0o700)
        self.outparent = pathlib.Path(self.tmp) / "out"
        self.outparent.mkdir(mode=0o700)
        (self.root / TARGET.lstrip("/")).mkdir(parents=True)
        for path in EXPECTED:
            (self.root / pathlib.Path(path.lstrip("/")).parent).mkdir(parents=True, exist_ok=True)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def put(self, path, data=b"sample"):
        p = self.root / path.lstrip("/")
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
        return p

    def run_acq(self, name="evidence", **env):
        out = self.outparent / name
        e = os.environ.copy()
        e["MONAD_M28_2_TEST_ROOT"] = str(self.root)
        for key in list(e):
            if key.startswith("MONAD_M28_2_LIMIT_"):
                del e[key]
        if "envkey" in env:
            e[env.pop("envkey")] = env.pop("envvalue")
        e.update(env)
        p = subprocess.run(["bash", str(SCRIPT), str(out)], env=e, text=True, capture_output=True)
        doc = None
        if (out / "inventory.json").exists():
            doc = json.loads((out / "inventory.json").read_text())
        return p, out, doc

    def result(self, doc, path):
        return [r for r in doc.get("results", []) if r["path"] == path]

    def test_valid_acquisition_and_checksum(self):
        p, out, d = self.run_acq()
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(d["completion"], "COMPLETE")
        raw = (out / "inventory.json").read_bytes()
        self.assertEqual((out / "SHA256SUMS").read_text(), hashlib.sha256(raw).hexdigest()+"  inventory.json\n")

    def test_optional_absent_and_missing_intermediate_differ(self):
        _, _, d = self.run_acq()
        self.assertEqual(self.result(d, "/var/lib/athena-alpha-m1/admin-install.lock")[0]["status"], "OPTIONAL_ABSENT")

    def test_missing_intermediate_directory_is_not_absence(self):
        shutil.rmtree(self.root / "usr")
        _, _, d = self.run_acq()
        self.assertEqual(self.result(d, "/usr/local/sbin/athena-alpha-m1-runner")[0]["status"], "MISSING_INTERMEDIATE_DIRECTORY")

    def test_symlink_target_and_descendant_are_rejected(self):
        base = self.root / TARGET.lstrip("/")
        (base / "link").symlink_to("/etc/passwd")
        (self.root / "etc").mkdir(exist_ok=True)
        (self.root / "etc" / "sudoers.d").mkdir(exist_ok=True)
        (self.root / "etc" / "sudoers.d" / "athena-alpha-m1").symlink_to("/etc/passwd")
        _, _, d = self.run_acq()
        statuses = [r["status"] for r in self.result(d, TARGET+"/link")]
        self.assertIn("SYMLINK_NOT_FOLLOWED", statuses)
        self.assertEqual(self.result(d, "/etc/sudoers.d/athena-alpha-m1")[0]["status"], "SYMLINK_NOT_FOLLOWED")

    def test_symlinked_ancestor_is_access_error(self):
        shutil.rmtree(self.root / "var")
        (self.root / "var").symlink_to("/tmp")
        _, _, d = self.run_acq()
        self.assertIn(self.result(d, TARGET)[0]["status"], ("PATH_ACCESS_ERROR", "MISSING_INTERMEDIATE_DIRECTORY"))

    def test_fifo_is_not_opened(self):
        b = self.root / TARGET.lstrip("/")
        os.mkfifo(b / "fifo")
        _, _, d = self.run_acq()
        self.assertEqual(self.result(d, TARGET+"/fifo")[0]["status"], "SPECIAL_FILE_NOT_OPENED")

    def test_unix_socket_is_not_opened(self):
        b = self.root / TARGET.lstrip("/")
        s = socket.socket(socket.AF_UNIX)
        try:
            s.bind(str(b / "s"))
            _, _, d = self.run_acq()
            self.assertEqual(self.result(d, TARGET+"/s")[0]["status"], "SPECIAL_FILE_NOT_OPENED")
        except OSError as exc:
            self.skipTest("sandbox does not permit synthetic Unix socket creation: "+str(exc))
        finally:
            s.close()

    def test_metadata_only_large_file_and_unauthorized_content_not_read(self):
        path = TARGET+"/backups/unknown.bin"
        self.put(path, b"x"*2_000_000)
        _, _, d = self.run_acq()
        r = self.result(d, path)[0]
        self.assertEqual(r["content_status"], "metadata_only_not_read")
        self.assertNotIn("sha256", r)
        self.assertEqual(d["bytes_hashed"], 0)

    def test_authorized_hash_exact_path(self):
        path = "/var/lib/athena-alpha-m1/admin-install.lock"
        self.put(path, b"")
        _, _, d = self.run_acq()
        r = self.result(d, path)[0]
        self.assertEqual(r["sha256"], hashlib.sha256(b"").hexdigest())
        self.assertEqual(d["hash_allowlist"], [path])

    def test_hash_read_error_is_recorded_partial(self):
        path = "/var/lib/athena-alpha-m1/admin-install.lock"
        self.put(path, b"")
        _, _, d = self.run_acq(envkey="MONAD_M28_2_TEST_HASH_ERROR", envvalue=path)
        self.assertEqual(self.result(d, path)[0]["status"], "HASH_READ_ERROR")
        self.assertEqual(d["completion"], "PARTIAL")

    def test_file_and_directory_mutations_are_recorded(self):
        fpath = "/var/lib/athena-alpha-m1/admin-install.lock"
        self.put(fpath, b"")
        _, _, d = self.run_acq(envkey="MONAD_M28_2_TEST_MUTATE_FILE", envvalue=fpath)
        self.assertEqual(self.result(d, fpath)[0]["status"], "CHANGED_DURING_ACQUISITION")
        dpath = TARGET
        _, _, d = self.run_acq("dir-mutation", envkey="MONAD_M28_2_TEST_MUTATE_DIRECTORY", envvalue=dpath)
        self.assertEqual(self.result(d, dpath)[0]["status"], "DIRECTORY_CHANGED_DURING_ACQUISITION")

    def test_concurrent_disappearance_and_identity_mismatch(self):
        path = TARGET+"/volatile"
        self.put(path, b"x")
        _, _, d = self.run_acq(envkey="MONAD_M28_2_TEST_DISAPPEAR", envvalue=path)
        self.assertEqual(self.result(d, path)[0]["status"], "DISAPPEARED_DURING_TRAVERSAL")
        self.put(path, b"x")
        _, _, d = self.run_acq("identity", envkey="MONAD_M28_2_TEST_IDENTITY_MISMATCH", envvalue=path)
        self.assertEqual(self.result(d, path)[0]["status"], "IDENTITY_MISMATCH")

    def test_failure_after_output_creation_preserves_destination(self):
        out = self.outparent / "after-create"
        e = os.environ.copy()
        e["MONAD_M28_2_TEST_ROOT"] = str(self.root)
        e["MONAD_M28_2_TEST_FAIL_AFTER_OUTPUT_CREATE"] = "1"
        p = subprocess.run(["bash", str(SCRIPT), str(out)], env=e, text=True, capture_output=True)
        self.assertEqual(p.returncode, 2)
        self.assertTrue(out.is_dir())
        self.assertIn("preserve created destination", p.stderr)

    def test_entry_depth_hash_and_output_limits_return_partial(self):
        self.put(TARGET+"/a", b"x")
        self.put(TARGET+"/nested/directory/file", b"y")
        for key, value in (("ENTRIES", "1"), ("DEPTH", "1"), ("OUTPUT", "4000")):
            p, _, d = self.run_acq("lim-"+key, **{"MONAD_M28_2_LIMIT_"+key: value})
            self.assertIsNotNone(d, p.stderr)
            self.assertEqual(d["completion"], "PARTIAL", key)
        payload = TARGET+"/payload"
        self.put(payload, b"payload")
        _, _, d = self.run_acq("lim-total", MONAD_M28_2_TEST_HASH_PATH=payload, MONAD_M28_2_LIMIT_TOTAL="1")
        self.assertEqual(d["completion"], "PARTIAL")

    def test_existing_output_destination_rejected(self):
        dest = self.outparent / "existing"
        dest.mkdir()
        p, _, _ = self.run_acq("existing")
        self.assertNotEqual(p.returncode, 0)

    def test_unsafe_output_parent_rejected(self):
        bad = self.outparent / "writable"
        bad.mkdir(mode=0o700)
        bad.chmod(0o777)
        p = subprocess.run(["bash", str(SCRIPT), str(bad / "evidence")], env={**os.environ, "MONAD_M28_2_TEST_ROOT": str(self.root)}, capture_output=True, text=True)
        self.assertNotEqual(p.returncode, 0)

    def test_output_normalization_and_symlink_parent_rejected(self):
        link = self.tmp + "/link"
        os.symlink(self.outparent, link)
        for dest in (str(self.outparent)+"/../bad", link+"/evidence"):
            p = subprocess.run(["bash", str(SCRIPT), dest], env={**os.environ, "MONAD_M28_2_TEST_ROOT": str(self.root)}, capture_output=True, text=True)
            self.assertNotEqual(p.returncode, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
