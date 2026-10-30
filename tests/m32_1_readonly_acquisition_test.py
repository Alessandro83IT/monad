#!/usr/bin/env python3
"""Synthetic-only M32 checks. Never invokes a privileged acquisition."""
import hashlib
import ast
import json
import os
import pathlib
import shutil
import socket
import subprocess
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "qualification/m32-1-readonly-acquisition.sh"
TARGET = "/var/lib/athena-alpha-m1"
EXPECTED = ["/usr/local/sbin/athena-alpha-m1-runner", "/usr/local/sbin/gateway_support.py",
 "/etc/sudoers.d/athena-alpha-m1", "/var/lib/athena-alpha-m1/admin-install.lock",
 "/var/lib/athena-alpha-m1", "/var/lib/athena-alpha-m1/backups", "/root/athena-alpha-m1-stage",
 "/run/athena-alpha-m1", "/var/log/athena-alpha-m1", "/var/lib/athena-alpha-m1/systemd-canary.json",
 "/var/lib/athena-alpha-m1/ext4-fixture", "/var/tmp/athena-d67-ext4", "/opt/athena-qualification/build"]


@unittest.skipIf(os.geteuid() == 0, "synthetic acquisition is intentionally forbidden as root")
class AcquisitionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="m32-")
        os.chmod(self.tmp, 0o700)
        self.root = pathlib.Path(self.tmp) / "fixture"
        self.root.mkdir(mode=0o700)
        self.outparent = pathlib.Path(self.tmp) / "out"
        self.outparent.mkdir(mode=0o700)
        (self.root / TARGET.lstrip("/")).mkdir(parents=True)
        (self.root / "etc").mkdir()
        (self.root / "usr/lib").mkdir(parents=True)
        (self.root / "usr/lib/os-release").write_text("ID=debian\nVERSION_ID=13\n")
        (self.root / "etc/os-release").write_text("ID=debian\nVERSION_ID=13\n")
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
        e["MONAD_M32_TEST_ROOT"] = str(self.root)
        for key in list(e):
            if key.startswith("MONAD_M32_LIMIT_"):
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

    def test_conditional_lock_absence_is_not_universally_required(self):
        (self.root / "var/lib/athena-alpha-m1/admin-install.lock").unlink()
        _, _, d = self.run_acq()
        r = self.result(d, "/var/lib/athena-alpha-m1/admin-install.lock")[0]
        self.assertEqual(r["status"], "CONDITIONAL_ABSENT")
        self.assertIn("qualification_relevance", r)
        self.assertEqual(d["completion"], "COMPLETE")

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
            _, _, d = self.run_acq(name, **{"MONAD_M32_TEST_"+name: path})
            self.assertEqual(self.result(d, path)[0]["status"], "FILESYSTEM_BOUNDARY")
            self.assertEqual(d["completion"], "PARTIAL")

    def test_mount_identity_unavailable_stops_conservatively(self):
        _, _, d = self.run_acq(envkey="MONAD_M32_TEST_MOUNT_UNAVAILABLE", envvalue="1")
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
        _, _, d = self.run_acq(envkey="MONAD_M32_TEST_HASH_ERROR", envvalue=path)
        self.assertEqual(self.result(d, path)[0]["status"], "HASH_READ_ERROR")
        self.assertEqual(d["completion"], "PARTIAL")

    def test_file_and_directory_mutations_are_recorded(self):
        fpath = "/var/lib/athena-alpha-m1/admin-install.lock"
        self.put(fpath, b"")
        _, _, d = self.run_acq(envkey="MONAD_M32_TEST_MUTATE_FILE", envvalue=fpath)
        self.assertEqual(self.result(d, fpath)[0]["status"], "CHANGED_DURING_ACQUISITION")
        dpath = TARGET
        _, _, d = self.run_acq("dir-mutation", envkey="MONAD_M32_TEST_MUTATE_DIRECTORY", envvalue=dpath)
        self.assertEqual(self.result(d, dpath)[0]["status"], "DIRECTORY_CHANGED_DURING_ACQUISITION")

    def test_concurrent_disappearance_and_identity_mismatch(self):
        path = TARGET+"/volatile"
        self.put(path, b"x")
        _, _, d = self.run_acq(envkey="MONAD_M32_TEST_DISAPPEAR", envvalue=path)
        self.assertEqual(self.result(d, path)[0]["status"], "DISAPPEARED_DURING_TRAVERSAL")
        self.put(path, b"x")
        _, _, d = self.run_acq("identity", envkey="MONAD_M32_TEST_IDENTITY_MISMATCH", envvalue=path)
        self.assertEqual(self.result(d, path)[0]["status"], "IDENTITY_MISMATCH")

    def test_failure_after_output_creation_preserves_destination(self):
        out = self.outparent / "after-create"
        e = os.environ.copy()
        e["MONAD_M32_TEST_ROOT"] = str(self.root)
        e["MONAD_M32_TEST_FAIL_AFTER_OUTPUT_CREATE"] = "1"
        p = subprocess.run(["bash", str(SCRIPT), str(out)], env=e, text=True, capture_output=True)
        self.assertEqual(p.returncode, 2)
        self.assertTrue(out.is_dir())
        self.assertIn("preserve created destination", p.stderr)

    def test_repeated_output_failures_do_not_leak_descriptors_to_caller(self):
        before = len(os.listdir("/proc/self/fd"))
        for index in range(4):
            out = self.outparent / f"fd-failure-{index}"
            env = os.environ.copy(); env["MONAD_M32_TEST_ROOT"] = str(self.root)
            env["MONAD_M32_TEST_FAIL_AFTER_OUTPUT_CREATE"] = "1"
            p = subprocess.run(["bash", str(SCRIPT), str(out)], env=env, text=True, capture_output=True)
            self.assertEqual(p.returncode, 2, p.stderr)
            self.assertTrue(out.is_dir())
        self.assertEqual(len(os.listdir("/proc/self/fd")), before)

    def test_output_write_failure_preserves_partial_evidence(self):
        out = self.outparent / "write-failure"
        e = os.environ.copy()
        e["MONAD_M32_TEST_ROOT"] = str(self.root)
        e["MONAD_M32_TEST_OUTPUT_WRITE_ERROR"] = "inventory.json"
        p = subprocess.run(["bash", str(SCRIPT), str(out)], env=e, text=True, capture_output=True)
        self.assertEqual(p.returncode, 2)
        self.assertTrue(out.is_dir())
        self.assertTrue((out / "inventory.json").exists())
        self.assertIn("injected output write failure", p.stderr)

    def test_entry_depth_hash_and_output_limits_return_partial(self):
        self.put(TARGET+"/a", b"x")
        self.put(TARGET+"/nested/directory/file", b"y")
        for key, value in (("ENTRIES", "1"), ("DEPTH", "1"), ("OUTPUT", "4000")):
            p, _, d = self.run_acq("lim-"+key, **{"MONAD_M32_LIMIT_"+key: value})
            self.assertIsNotNone(d, p.stderr)
            self.assertEqual(d["completion"], "PARTIAL", key)
        payload = TARGET+"/payload"
        self.put(payload, b"payload")
        _, _, d = self.run_acq("lim-total", MONAD_M32_TEST_HASH_PATH=payload, MONAD_M32_LIMIT_TOTAL="1")
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
        p = subprocess.run(["bash", str(SCRIPT), str(bad / "evidence")], env={**os.environ, "MONAD_M32_TEST_ROOT": str(self.root)}, capture_output=True, text=True)
        self.assertNotEqual(p.returncode, 0)

    def test_output_normalization_and_symlink_parent_rejected(self):
        link = self.tmp + "/link"
        os.symlink(self.outparent, link)
        for dest in (str(self.outparent)+"/../bad", link+"/evidence"):
            p = subprocess.run(["bash", str(SCRIPT), dest], env={**os.environ, "MONAD_M32_TEST_ROOT": str(self.root)}, capture_output=True, text=True)
            self.assertNotEqual(p.returncode, 0)


    def test_mount_identity_unavailable_is_partial(self):
        p, _, d = self.run_acq(envkey="MONAD_M32_TEST_MOUNT_UNAVAILABLE", envvalue="1")
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(d["completion"], "PARTIAL")
        self.assertTrue(any(r["status"] == "MOUNT_ID_UNAVAILABLE" for r in d["results"]))

    def test_output_fsync_failures_preserve_created_artifacts(self):
        out = self.outparent / "fsync-file"
        e = os.environ.copy(); e["MONAD_M32_TEST_ROOT"] = str(self.root)
        e["MONAD_M32_TEST_FSYNC_ERROR"] = "inventory.json"
        p = subprocess.run(["bash", str(SCRIPT), str(out)], env=e, text=True, capture_output=True)
        self.assertEqual(p.returncode, 2); self.assertTrue((out / "inventory.json").exists())
        out2 = self.outparent / "fsync-dir"
        e.pop("MONAD_M32_TEST_FSYNC_ERROR"); e["MONAD_M32_TEST_DIR_FSYNC_ERROR"] = "1"
        p = subprocess.run(["bash", str(SCRIPT), str(out2)], env=e, text=True, capture_output=True)
        self.assertEqual(p.returncode, 2); self.assertTrue((out2 / "SHA256SUMS").exists())

    def test_short_writes_are_completed_and_checksum_valid(self):
        p, out, d = self.run_acq(envkey="MONAD_M32_TEST_SHORT_WRITE", envvalue="inventory.json")
        self.assertEqual(p.returncode, 0, p.stderr)
        raw = (out / "inventory.json").read_bytes()
        self.assertEqual((out / "SHA256SUMS").read_text(), hashlib.sha256(raw).hexdigest()+"  inventory.json\n")

    def test_zero_progress_write_is_not_reported_durable(self):
        out = self.outparent / "zero-write"
        e = os.environ.copy(); e["MONAD_M32_TEST_ROOT"] = str(self.root)
        e["MONAD_M32_TEST_ZERO_WRITE"] = "inventory.json"
        p = subprocess.run(["bash", str(SCRIPT), str(out)], env=e, text=True, capture_output=True)
        self.assertEqual(p.returncode, 2)
        self.assertIn("OUTPUT_NOT_DURABLE", p.stderr)
        self.assertTrue((out / "inventory.json").exists())

    def test_production_identity_policy_pins_debian_release_symlink_target(self):
        source = SCRIPT.read_text()
        self.assertIn('os.stat("os-release",dir_fd=etc_fd,follow_symlinks=False)', source)
        self.assertIn('target!="../usr/lib/os-release"', source)
        self.assertNotIn('realpath("/etc/os-release")', source)
        self.assertIn('os.O_NOFOLLOW)', source)
        self.assertIn('release_version!="13"', source)
        self.assertIn('9bdbfd8678b5467cb85e4d8aa5e01fea', source)
        self.assertIn('VirtualBox-e475ea91-72e9-4157-9ee6-583e9ae0462f', source)
        self.assertIn('91ea75e4-e972-5741-9ee6-583e9ae0462f', source)

    def test_os_release_regular_and_trusted_symlink_layouts(self):
        e = {"MONAD_M32_TEST_OS_RELEASE_CHECK": "1"}
        p, _, d = self.run_acq("release-regular", **e)
        self.assertEqual(p.returncode, 0, p.stderr)
        (self.root / "etc/os-release").unlink()
        (self.root / "etc/os-release").symlink_to("../usr/lib/os-release")
        p, _, d = self.run_acq("release-symlink", **e)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(d["identity"]["machine_id"], "synthetic-machine-id")

    def test_os_release_replacement_and_untrusted_targets_are_refused(self):
        (self.root / "etc/os-release").unlink()
        (self.root / "etc/os-release").symlink_to("../usr/lib/os-release")
        p, _, _ = self.run_acq("release-replacement", MONAD_M32_TEST_OS_RELEASE_CHECK="1",
                               MONAD_M32_TEST_REPLACE_OS_RELEASE_TARGET="1")
        self.assertEqual(p.returncode, 2)
        (self.root / "etc/os-release").unlink()
        (self.root / "etc/os-release").symlink_to("/etc/passwd")
        p, _, _ = self.run_acq("release-untrusted", MONAD_M32_TEST_OS_RELEASE_CHECK="1")
        self.assertEqual(p.returncode, 2)

    def test_output_setup_faults_are_reported_and_destinations_preserved(self):
        for fault in ("FAIL_MKDIRAT", "FAIL_OPEN_OUTPUT", "FAIL_FSTAT_OUTPUT", "FAIL_DUP"):
            p, out, _ = self.run_acq("setup-"+fault.lower(), **{"MONAD_M32_TEST_"+fault: "1"})
            self.assertEqual(p.returncode, 2, (fault, p.stderr))
            if fault != "FAIL_MKDIRAT": self.assertTrue(out.is_dir(), fault)
        component=self.outparent.name
        p, out, _ = self.run_acq("setup-parent-fstat", MONAD_M32_TEST_FAIL_PARENT_FSTAT_COMPONENT=component)
        self.assertEqual(p.returncode, 2, p.stderr)
        self.assertFalse(out.exists())

    def test_file_exists_and_errno_failures_are_conservative(self):
        dest = self.outparent / "collision"
        dest.mkdir()
        p, _, _ = self.run_acq("collision")
        self.assertNotEqual(p.returncode, 0)
        for code in (23, 28, 13, 1, 116):  # EMFILE, ENFILE, EACCES, EPERM, ESTALE
            path = TARGET+"/errno-"+str(code)
            self.put(path)
            _, _, doc = self.run_acq("errno-"+str(code), MONAD_M32_TEST_OPEN_ERRNO_PATH=path,
                                     MONAD_M32_TEST_OPEN_ERRNO=str(code))
            self.assertEqual(doc["completion"], "PARTIAL", code)
            self.assertEqual(self.result(doc, path)[0]["status"], "OPEN_ERROR")

    def test_os_release_parser_requires_unique_exact_fields(self):
        source = SCRIPT.read_text().split("<<'PY'\n", 1)[1].rsplit("\nPY", 1)[0]
        module = ast.parse(source)
        function = next(n for n in module.body if isinstance(n, ast.FunctionDef) and n.name == "os_release_field")
        namespace = {}
        exec(compile(ast.Module(body=[function], type_ignores=[]), "m32-os-release-parser", "exec"), namespace)
        parse_field = namespace["os_release_field"]
        self.assertEqual(parse_field('ID=debian\nVERSION_ID="13"\n', "ID"), "debian")
        self.assertEqual(parse_field('ID=debian\nVERSION_ID=13\n', "VERSION_ID"), "13")
        with self.assertRaises(OSError): parse_field('NOT_ID=debian\n', "ID")
        with self.assertRaises(OSError): parse_field('ID=debian\nID=debian\n', "ID")

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
        p, _, d = self.run_acq(envkey="MONAD_M32_LIMIT_ENTRIES", envvalue="4")
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(d["completion"], "PARTIAL")
        self.assertLessEqual(d["entry_count"], 4)

    def test_exact_and_one_beyond_traversal_budget_are_conservative(self):
        self.put(TARGET+"/budget-child", b"x")
        p, _, baseline = self.run_acq("traversal-baseline")
        self.assertEqual(p.returncode, 0, p.stderr)
        used = baseline["entry_count"]
        for limit in (used-1, used, used+1):
            p, _, d = self.run_acq(f"traversal-{limit}", MONAD_M32_LIMIT_ENTRIES=str(limit))
            self.assertLessEqual(d["entry_count"], limit)
            if limit == used-1:
                self.assertEqual(d["completion"], "PARTIAL")
                self.assertTrue(any(r["status"] == "LIMIT_ENTRIES" for r in d["results"]))
            else:
                self.assertEqual(d["completion"], "COMPLETE")

    def test_output_files_and_directory_have_restricted_modes(self):
        p, out, _ = self.run_acq("permissions")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(out.stat().st_mode & 0o777, 0o700)
        for name in ("inventory.json", "SHA256SUMS"):
            self.assertEqual((out / name).stat().st_mode & 0o777, 0o600)

    def test_enumeration_budget_is_global_and_counts_verification_and_lookahead(self):
        # A wide directory forces look-ahead; a nested directory forces
        # independent first and verification passes against the same budget.
        for i in range(24): self.put(TARGET+f"/wide-{i}", b"x")
        self.put(TARGET+"/nested/deeper/leaf", b"x")
        p, _, baseline = self.run_acq("enum-baseline")
        self.assertEqual(p.returncode, 0, p.stderr)
        consumed = baseline["enumeration_entries_consumed"]
        self.assertGreater(consumed, 24)
        # Exact exhaustion is intentionally conservative: the collector cannot
        # ask scandir for another item without risking consuming entry N+1.
        for limit in (consumed, consumed-1):
            p, _, d = self.run_acq(f"enum-{limit}", MONAD_M32_LIMIT_ENUMERATION=str(limit))
            self.assertLessEqual(d["enumeration_entries_consumed"], limit)
            self.assertEqual(d["completion"], "PARTIAL")
            self.assertTrue(any("INCOMPLETE_ENUMERATION_BUDGET" in str(r) for r in d["results"]))
        p, _, d = self.run_acq("enum-over", MONAD_M32_LIMIT_ENUMERATION=str(consumed+1))
        self.assertLessEqual(d["enumeration_entries_consumed"], consumed+1)
        self.assertEqual(d["completion"], "COMPLETE")

    def test_parent_directory_fsync_is_required_and_destination_preserved(self):
        p, out, d = self.run_acq("parent-sync-ok")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertTrue(out.is_dir())
        self.assertIn("anchored parent", d["output_durability"])
        out = self.outparent / "parent-sync-fail"
        env = os.environ.copy(); env["MONAD_M32_TEST_ROOT"] = str(self.root)
        env["MONAD_M32_TEST_PARENT_FSYNC_ERROR"] = "1"
        p = subprocess.run(["bash", str(SCRIPT), str(out)], env=env, text=True, capture_output=True)
        self.assertEqual(p.returncode, 2)
        self.assertTrue(out.is_dir())
        self.assertFalse((out / "inventory.json").exists())
        self.assertIn("parent fsync failure", p.stderr)

    def test_root_path_replacement_does_not_redirect_traversal(self):
        self.put(TARGET+"/root-marker", b"anchored")
        p, out, d = self.run_acq(envkey="MONAD_M32_TEST_REPLACE_ROOT", envvalue="1")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(d["completion"], "COMPLETE")
        moved = pathlib.Path(str(self.root) + ".anchored")
        self.assertTrue((moved / TARGET.lstrip("/")).is_dir())
        self.assertTrue(self.root.is_symlink())
        self.assertEqual(self.result(d, TARGET+"/root-marker")[0]["status"], "PRESENT")

    def test_output_directory_replacement_keeps_writes_on_open_descriptor(self):
        out = self.outparent / "replace-output"
        env = os.environ.copy(); env["MONAD_M32_TEST_ROOT"] = str(self.root)
        env["MONAD_M32_TEST_REPLACE_OUTPUT"] = "1"
        p = subprocess.run(["bash", str(SCRIPT), str(out)], env=env, text=True, capture_output=True)
        self.assertEqual(p.returncode, 2, p.stderr)
        self.assertTrue((self.outparent / "replace-output.anchored" / "inventory.json").is_file())
        self.assertFalse((out / "inventory.json").exists())

    def test_output_parent_replacement_is_detected_without_redirection(self):
        out = self.outparent / "parent-race-evidence"
        env = os.environ.copy(); env["MONAD_M32_TEST_ROOT"] = str(self.root)
        env["MONAD_M32_TEST_REPLACE_OUTPUT_PARENT"] = "1"
        p = subprocess.run(["bash", str(SCRIPT), str(out)], env=env, text=True, capture_output=True)
        self.assertEqual(p.returncode, 2, p.stderr)
        self.assertIn("output parent replaced", p.stderr)
        self.assertTrue((pathlib.Path(str(self.outparent)+".anchored") / out.name / "inventory.json").is_file())
        self.assertFalse((out / "inventory.json").exists())

    def test_low_descriptor_limit_and_deep_tree_are_partial_without_crash(self):
        deep = TARGET + "/" + "/".join("d" + str(i) for i in range(45)) + "/leaf"
        self.put(deep, b"x")
        def lower_limit():
            import resource
            resource.setrlimit(resource.RLIMIT_NOFILE, (12, 12))
        out = self.outparent / "low-fd"
        env = os.environ.copy(); env["MONAD_M32_TEST_ROOT"] = str(self.root)
        p = subprocess.run(["bash", str(SCRIPT), str(out)], env=env, text=True,
                           capture_output=True, preexec_fn=lower_limit)
        self.assertIn(p.returncode, (0, 1))
        d = json.loads((out / "inventory.json").read_text())
        self.assertEqual(d["completion"], "PARTIAL")

    def test_emfile_during_traversal_is_partial(self):
        path = TARGET+"/emfile"
        self.put(path)
        _, _, d = self.run_acq(envkey="MONAD_M32_TEST_EMFILE_PATH", envvalue=path)
        self.assertEqual(self.result(d, path)[0]["status"], "OPEN_ERROR")
        self.assertEqual(d["completion"], "PARTIAL")

    def test_schema_is_consistent_for_size_limited_partial(self):
        p, _, d = self.run_acq(envkey="MONAD_M32_LIMIT_OUTPUT", envvalue="4000")
        self.assertIsNotNone(d, p.stderr)
        self.assertEqual(d["schema"], 4)
        self.assertEqual(d["completion"], "PARTIAL")
        self.assertIn("results", d)
        _, _, complete = self.run_acq("schema-complete")
        self.assertEqual(set(d), set(complete))

    def test_conditional_and_historical_absence_are_classified(self):
        (self.root / "var/lib/athena-alpha-m1/systemd-canary.json").unlink()
        shutil.rmtree(self.root / "var/tmp/athena-d67-ext4")
        _, _, d = self.run_acq()
        canary = self.result(d, "/var/lib/athena-alpha-m1/systemd-canary.json")[0]
        historical = self.result(d, "/var/tmp/athena-d67-ext4")[0]
        self.assertEqual(canary["status"], "CONDITIONAL_ABSENT")
        self.assertEqual(historical["status"], "HISTORICAL_TARGET_CURRENTLY_ABSENT")
        self.assertFalse(historical["historical_absence_inferred"])

    def test_stable_directory_at_entry_limit_is_not_called_changed(self):
        for i in range(5): self.put(TARGET+f"/stable-{i}")
        _, _, d = self.run_acq(envkey="MONAD_M32_LIMIT_ENTRIES", envvalue="8")
        result = self.result(d, TARGET)[0]
        self.assertNotEqual(result["status"], "DIRECTORY_CHANGED_DURING_ACQUISITION")

    def test_ancestor_symlink_replacement_does_not_escape_root_descriptor(self):
        path = TARGET+"/ancestor-marker"
        self.put(path)
        p, _, d = self.run_acq(envkey="MONAD_M32_TEST_REPLACE_ROOT", envvalue="1")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(self.result(d, path)[0]["status"], "PRESENT")


if __name__ == "__main__":
    unittest.main(verbosity=2)
