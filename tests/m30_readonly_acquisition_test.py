#!/usr/bin/env python3
"""Synthetic-only M30 checks. Never invokes a privileged acquisition."""
import hashlib
import json
import os
import pathlib
import shutil
import socket
import subprocess
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "qualification/m30-readonly-acquisition.sh"
TARGET = "/var/lib/athena-alpha-m1"
EXPECTED = ["/usr/local/sbin/athena-alpha-m1-runner", "/usr/local/sbin/gateway_support.py",
 "/etc/sudoers.d/athena-alpha-m1", "/var/lib/athena-alpha-m1/admin-install.lock",
 "/var/lib/athena-alpha-m1", "/var/lib/athena-alpha-m1/backups", "/root/athena-alpha-m1-stage",
 "/run/athena-alpha-m1", "/var/log/athena-alpha-m1", "/var/lib/athena-alpha-m1/systemd-canary.json",
 "/var/lib/athena-alpha-m1/ext4-fixture", "/var/tmp/athena-d67-ext4", "/opt/athena-qualification/build"]


@unittest.skipIf(os.geteuid() == 0, "synthetic acquisition is intentionally forbidden as root")
class AcquisitionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="m30-")
        os.chmod(self.tmp, 0o700)
        self.root = pathlib.Path(self.tmp) / "fixture"
        self.root.mkdir(mode=0o700)
        self.outparent = pathlib.Path(self.tmp) / "out"
        self.outparent.mkdir(mode=0o700)
        (self.root / TARGET.lstrip("/")).mkdir(parents=True)
        for path in EXPECTED:
            (self.root / pathlib.Path(path.lstrip("/")).parent).mkdir(parents=True, exist_ok=True)
        for path in EXPECTED:
            p = self.root / path.lstrip("/")
            if path in ("/usr/local/sbin/athena-alpha-m1-runner", "/usr/local/sbin/gateway_support.py", "/etc/sudoers.d/athena-alpha-m1", "/var/lib/athena-alpha-m1/admin-install.lock", "/var/lib/athena-alpha-m1/systemd-canary.json"):
                p.touch()
            else:
                p.mkdir(exist_ok=True)

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
        e["MONAD_M30_TEST_ROOT"] = str(self.root)
        for key in list(e):
            if key.startswith("MONAD_M30_LIMIT_"):
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

    def test_optional_absent_and_required_absent_differ(self):
        shutil.rmtree(self.root / "run" / "athena-alpha-m1")
        p, _, d = self.run_acq()
        self.assertEqual(self.result(d, "/run/athena-alpha-m1")[0]["status"], "OPTIONAL_ABSENT")
        self.assertEqual(self.result(d, "/var/lib/athena-alpha-m1/admin-install.lock")[0]["status"], "PRESENT")
        self.assertEqual(d["completion"], "COMPLETE")

    def test_required_absent_is_partial(self):
        (self.root / "var/lib/athena-alpha-m1/admin-install.lock").unlink()
        _, _, d = self.run_acq()
        r = self.result(d, "/var/lib/athena-alpha-m1/admin-install.lock")[0]
        self.assertEqual(r["status"], "REQUIRED_ABSENT")
        self.assertFalse(r["historical_absence_inferred"])
        self.assertEqual(d["completion"], "PARTIAL")

    def test_missing_intermediate_directory_is_not_absence(self):
        shutil.rmtree(self.root / "usr")
        _, _, d = self.run_acq()
        self.assertEqual(self.result(d, "/usr/local/sbin/athena-alpha-m1-runner")[0]["status"], "MISSING_INTERMEDIATE_DIRECTORY")

    def test_symlink_target_and_descendant_are_rejected(self):
        base = self.root / TARGET.lstrip("/")
        (base / "link").symlink_to("/etc/passwd")
        (self.root / "etc").mkdir(exist_ok=True)
        (self.root / "etc" / "sudoers.d").mkdir(exist_ok=True)
        (self.root / "etc" / "sudoers.d" / "athena-alpha-m1").unlink()
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

    def test_device_and_mount_boundaries_are_partial(self):
        path = TARGET+"/backups"
        for name in ("DEVICE_BOUNDARY", "MOUNT_BOUNDARY"):
            _, _, d = self.run_acq(name, **{"MONAD_M30_TEST_"+name: path})
            self.assertEqual(self.result(d, path)[0]["status"], "FILESYSTEM_BOUNDARY")
            self.assertEqual(d["completion"], "PARTIAL")

    def test_mount_identity_unavailable_stops_conservatively(self):
        _, _, d = self.run_acq(envkey="MONAD_M30_TEST_MOUNT_UNAVAILABLE", envvalue="1")
        self.assertEqual(d["completion"], "PARTIAL")
        self.assertTrue(any(r["status"] == "MOUNT_ID_UNAVAILABLE" for r in d["results"]))

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
        _, _, d = self.run_acq(envkey="MONAD_M30_TEST_HASH_ERROR", envvalue=path)
        self.assertEqual(self.result(d, path)[0]["status"], "HASH_READ_ERROR")
        self.assertEqual(d["completion"], "PARTIAL")

    def test_file_and_directory_mutations_are_recorded(self):
        fpath = "/var/lib/athena-alpha-m1/admin-install.lock"
        self.put(fpath, b"")
        _, _, d = self.run_acq(envkey="MONAD_M30_TEST_MUTATE_FILE", envvalue=fpath)
        self.assertEqual(self.result(d, fpath)[0]["status"], "CHANGED_DURING_ACQUISITION")
        dpath = TARGET
        _, _, d = self.run_acq("dir-mutation", envkey="MONAD_M30_TEST_MUTATE_DIRECTORY", envvalue=dpath)
        self.assertEqual(self.result(d, dpath)[0]["status"], "DIRECTORY_CHANGED_DURING_ACQUISITION")

    def test_concurrent_disappearance_and_identity_mismatch(self):
        path = TARGET+"/volatile"
        self.put(path, b"x")
        _, _, d = self.run_acq(envkey="MONAD_M30_TEST_DISAPPEAR", envvalue=path)
        self.assertEqual(self.result(d, path)[0]["status"], "DISAPPEARED_DURING_TRAVERSAL")
        self.put(path, b"x")
        _, _, d = self.run_acq("identity", envkey="MONAD_M30_TEST_IDENTITY_MISMATCH", envvalue=path)
        self.assertEqual(self.result(d, path)[0]["status"], "IDENTITY_MISMATCH")

    def test_failure_after_output_creation_preserves_destination(self):
        out = self.outparent / "after-create"
        e = os.environ.copy()
        e["MONAD_M30_TEST_ROOT"] = str(self.root)
        e["MONAD_M30_TEST_FAIL_AFTER_OUTPUT_CREATE"] = "1"
        p = subprocess.run(["bash", str(SCRIPT), str(out)], env=e, text=True, capture_output=True)
        self.assertEqual(p.returncode, 2)
        self.assertTrue(out.is_dir())
        self.assertIn("preserve created destination", p.stderr)

    def test_output_write_failure_preserves_partial_evidence(self):
        out = self.outparent / "write-failure"
        e = os.environ.copy()
        e["MONAD_M30_TEST_ROOT"] = str(self.root)
        e["MONAD_M30_TEST_OUTPUT_WRITE_ERROR"] = "inventory.json"
        p = subprocess.run(["bash", str(SCRIPT), str(out)], env=e, text=True, capture_output=True)
        self.assertEqual(p.returncode, 2)
        self.assertTrue(out.is_dir())
        self.assertTrue((out / "inventory.json").exists())
        self.assertIn("injected output write failure", p.stderr)

    def test_entry_depth_hash_and_output_limits_return_partial(self):
        self.put(TARGET+"/a", b"x")
        self.put(TARGET+"/nested/directory/file", b"y")
        for key, value in (("ENTRIES", "1"), ("DEPTH", "1"), ("OUTPUT", "4000")):
            p, _, d = self.run_acq("lim-"+key, **{"MONAD_M30_LIMIT_"+key: value})
            self.assertIsNotNone(d, p.stderr)
            self.assertEqual(d["completion"], "PARTIAL", key)
        payload = TARGET+"/payload"
        self.put(payload, b"payload")
        _, _, d = self.run_acq("lim-total", MONAD_M30_TEST_HASH_PATH=payload, MONAD_M30_LIMIT_TOTAL="1")
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
        p = subprocess.run(["bash", str(SCRIPT), str(bad / "evidence")], env={**os.environ, "MONAD_M30_TEST_ROOT": str(self.root)}, capture_output=True, text=True)
        self.assertNotEqual(p.returncode, 0)

    def test_output_normalization_and_symlink_parent_rejected(self):
        link = self.tmp + "/link"
        os.symlink(self.outparent, link)
        for dest in (str(self.outparent)+"/../bad", link+"/evidence"):
            p = subprocess.run(["bash", str(SCRIPT), dest], env={**os.environ, "MONAD_M30_TEST_ROOT": str(self.root)}, capture_output=True, text=True)
            self.assertNotEqual(p.returncode, 0)


    def test_mount_identity_unavailable_is_partial(self):
        p, _, d = self.run_acq(envkey="MONAD_M30_TEST_MOUNT_UNAVAILABLE", envvalue="1")
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(d["completion"], "PARTIAL")
        self.assertTrue(any(r["status"] == "MOUNT_ID_UNAVAILABLE" for r in d["results"]))

    def test_output_fsync_failures_preserve_created_artifacts(self):
        out = self.outparent / "fsync-file"
        e = os.environ.copy(); e["MONAD_M30_TEST_ROOT"] = str(self.root)
        e["MONAD_M30_TEST_FSYNC_ERROR"] = "inventory.json"
        p = subprocess.run(["bash", str(SCRIPT), str(out)], env=e, text=True, capture_output=True)
        self.assertEqual(p.returncode, 2); self.assertTrue((out / "inventory.json").exists())
        out2 = self.outparent / "fsync-dir"
        e.pop("MONAD_M30_TEST_FSYNC_ERROR"); e["MONAD_M30_TEST_DIR_FSYNC_ERROR"] = "1"
        p = subprocess.run(["bash", str(SCRIPT), str(out2)], env=e, text=True, capture_output=True)
        self.assertEqual(p.returncode, 2); self.assertTrue((out2 / "SHA256SUMS").exists())

    def test_short_writes_are_completed_and_checksum_valid(self):
        p, out, d = self.run_acq(envkey="MONAD_M30_TEST_SHORT_WRITE", envvalue="inventory.json")
        self.assertEqual(p.returncode, 0, p.stderr)
        raw = (out / "inventory.json").read_bytes()
        self.assertEqual((out / "SHA256SUMS").read_text(), hashlib.sha256(raw).hexdigest()+"  inventory.json\n")

    def test_test_environment_does_not_escape_allowlist(self):
        outside = pathlib.Path(self.tmp) / "outside-secret"
        outside.write_text("must not be read")
        _, _, d = self.run_acq()
        self.assertFalse(any(str(outside) in json.dumps(r) for r in d["results"]))
        self.assertEqual(d["bytes_hashed"], 0)

    def test_ancestor_symlink_and_optional_parent_failure_are_not_absence(self):
        shutil.rmtree(self.root / "run")
        (self.root / "elsewhere").mkdir()
        (self.root / "run").symlink_to(self.root / "elsewhere", target_is_directory=True)
        _, _, d = self.run_acq()
        r = self.result(d, "/run/athena-alpha-m1")[0]
        self.assertNotEqual(r["status"], "OPTIONAL_ABSENT")

    def test_missing_optional_leaf_is_only_optional_absence(self):
        shutil.rmtree(self.root / "run" / "athena-alpha-m1")
        _, _, d = self.run_acq()
        self.assertEqual(self.result(d, "/run/athena-alpha-m1")[0]["status"], "OPTIONAL_ABSENT")

    def test_entry_limit_bounds_inventory_and_reports_partial(self):
        for i in range(30): self.put(TARGET+f"/bulk-{i}", b"x")
        p, _, d = self.run_acq(envkey="MONAD_M30_LIMIT_ENTRIES", envvalue="4")
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(d["completion"], "PARTIAL")
        self.assertLessEqual(d["entry_count"], 4)

    def test_root_path_replacement_does_not_redirect_traversal(self):
        p, out, d = self.run_acq(envkey="MONAD_M30_TEST_REPLACE_ROOT", envvalue="1")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(d["completion"], "COMPLETE")
        moved = pathlib.Path(str(self.root) + ".anchored")
        self.assertTrue((moved / TARGET.lstrip("/")).is_dir())
        self.assertTrue(self.root.is_symlink())

    def test_output_directory_replacement_keeps_writes_on_open_descriptor(self):
        out = self.outparent / "replace-output"
        env = os.environ.copy(); env["MONAD_M30_TEST_ROOT"] = str(self.root)
        env["MONAD_M30_TEST_REPLACE_OUTPUT"] = "1"
        p = subprocess.run(["bash", str(SCRIPT), str(out)], env=env, text=True, capture_output=True)
        self.assertEqual(p.returncode, 2, p.stderr)
        self.assertTrue((self.outparent / "replace-output.anchored" / "inventory.json").is_file())
        self.assertFalse((out / "inventory.json").exists())

    def test_low_descriptor_limit_and_deep_tree_are_partial_without_crash(self):
        deep = TARGET + "/" + "/".join("d" + str(i) for i in range(45)) + "/leaf"
        self.put(deep, b"x")
        def lower_limit():
            import resource
            resource.setrlimit(resource.RLIMIT_NOFILE, (12, 12))
        out = self.outparent / "low-fd"
        env = os.environ.copy(); env["MONAD_M30_TEST_ROOT"] = str(self.root)
        p = subprocess.run(["bash", str(SCRIPT), str(out)], env=env, text=True,
                           capture_output=True, preexec_fn=lower_limit)
        self.assertIn(p.returncode, (0, 1))
        d = json.loads((out / "inventory.json").read_text())
        self.assertEqual(d["completion"], "PARTIAL")
        self.assertEqual(d["completion"], "PARTIAL")


if __name__ == "__main__":
    unittest.main(verbosity=2)
