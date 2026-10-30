#!/usr/bin/env python3
"""Independent synthetic adversarial verification for the M34 collector.

Tests assert manifest semantics and captured bytes, not only process status.
All source/output fixtures are private temporary directories.
"""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]
SCRIPT = REPO / "qualification/m34-1-targeted-forensic-acquisition.sh"
ROOTS = ("usr/local/sbin", "etc/sudoers.d", "root/athena-alpha-m1-stage",
         "var/lib/athena-alpha-m1",
         "var/lib/monad-m32-1-evidence-20261029T194120Z")
M32 = "var/lib/monad-m32-1-evidence-20261029T194120Z"
BACKUP = "var/lib/athena-alpha-m1/backups/run-20261021-061921"


class IndependentVerification(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="m34-4-independent-")
        self.base = Path(self.tmp.name)
        self.src, self.dest = self.base / "source", self.base / "dest"
        self.src.mkdir(mode=0o700)
        self.dest.mkdir(mode=0o700)
        for rel in ROOTS:
            (self.src / rel).mkdir(parents=True)
        (self.src / BACKUP).mkdir(parents=True)
        self.put(f"{M32}/inventory.json", b'{"fixture":true}\n')
        self.valid_sums()
        self.put("usr/local/sbin/athena-alpha-m1-runner", b"runner payload")
        self.put("usr/local/sbin/gateway_support.py", b"support payload")
        self.put("etc/sudoers.d/athena-alpha-m1", b"sudoers payload")
        for d in sorted((p for p in self.src.rglob("*") if p.is_dir()),
                        key=lambda p: len(p.parts), reverse=True):
            d.chmod(0o755)

    def tearDown(self):
        self.tmp.cleanup()

    def put(self, rel, data=b"fixture"):
        path = self.src / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        path.chmod(0o600)
        return path

    def valid_sums(self):
        data = (self.src / f"{M32}/inventory.json").read_bytes()
        self.put(f"{M32}/SHA256SUMS",
                 (hashlib.sha256(data).hexdigest() + "  inventory.json\n").encode())

    def run_collector(self, name="result", **controls):
        env = os.environ.copy()
        env["MONAD_M34_TEST_ROOT"] = str(self.src)
        env.update({k: str(v) for k, v in controls.items()})
        return subprocess.run(["bash", str(SCRIPT), str(self.dest / name)], env=env,
                              text=True, capture_output=True, timeout=20)

    def inventory(self, name="result"):
        return json.loads((self.dest / name / "inventory.json").read_text())

    def assert_not_complete(self, proc, name="result"):
        inv = self.inventory(name)
        self.assertNotEqual(inv["completion"], "COMPLETE")
        self.assertNotEqual(proc.returncode, 0)
        return inv

    def test_required_file_directory_and_required_directory_file(self):
        runner = self.src / "usr/local/sbin/athena-alpha-m1-runner"
        runner.unlink(); runner.mkdir(mode=0o700)
        p = self.run_collector()
        inv = self.assert_not_complete(p)
        row = next(r for r in inv["results"] if r["path"].endswith("athena-alpha-m1-runner"))
        self.assertEqual((row["status"], row["expected_type"], inv["completion"]),
                         ("TYPE_MISMATCH", "file", "PARTIAL"))
        shutil.rmtree(self.dest / "result")
        backup = self.src / BACKUP
        shutil.rmtree(backup); backup.write_bytes(b"wrong type"); backup.chmod(0o600)
        p = self.run_collector()
        inv = self.assert_not_complete(p)
        row = next(r for r in inv["results"] if r["path"] == "/" + BACKUP)
        self.assertEqual((row["status"], row["expected_type"]), ("TYPE_MISMATCH", "directory"))

    def test_missing_and_inaccessible_required_evidence(self):
        (self.src / "etc/sudoers.d/athena-alpha-m1").unlink()
        p = self.run_collector()
        inv = self.assert_not_complete(p)
        self.assertTrue(any(i.get("path", "").endswith("athena-alpha-m1") and
                            i["status"] == "REQUIRED_EVIDENCE_UNAVAILABLE" for i in inv["issues"]))
        shutil.rmtree(self.dest / "result")
        self.put("etc/sudoers.d/athena-alpha-m1", b"sudoers payload")
        sudoers = self.src / "etc/sudoers.d/athena-alpha-m1"
        sudoers.chmod(0)
        try:
            p = self.run_collector()
        finally:
            sudoers.chmod(0o600)
        inv = self.assert_not_complete(p)
        self.assertTrue(any(r.get("status") in ("PERMISSION_ERROR", "OPEN_ERROR", "READ_ERROR")
                            for r in inv["results"]))
        shutil.rmtree(self.dest / "result")
        blocked = self.src / "usr/local/sbin"
        blocked.chmod(0)
        try:
            p = self.run_collector()
        finally:
            blocked.chmod(0o755)
        inv = self.assert_not_complete(p)
        self.assertTrue(inv["issues"])
        self.assertNotEqual(inv["completion"], "COMPLETE")

    def test_no_digest_required_file_is_not_complete(self):
        runner = self.src / "usr/local/sbin/athena-alpha-m1-runner"
        runner.unlink(); runner.symlink_to("/dev/null")
        p = self.run_collector()
        inv = self.assert_not_complete(p)
        self.assertIn("SYMLINK_NOT_FOLLOWED", {r["status"] for r in inv["results"]})
        self.assertTrue(any(i["status"] == "REQUIRED_EVIDENCE_UNAVAILABLE" for i in inv["issues"]))

    def test_incomplete_manifest_and_bad_source_checksum_are_classified(self):
        (self.src / f"{M32}/SHA256SUMS").write_text("f" * 64 + "  inventory.json\n")
        p = self.run_collector()
        inv = self.assert_not_complete(p)
        self.assertIn("SOURCE_CHECKSUM_INVALID", {i["status"] for i in inv["issues"]})
        shutil.rmtree(self.dest / "result")
        (self.src / "etc/sudoers.d/athena-alpha-m1").unlink()
        self.valid_sums()
        p = self.run_collector()
        inv = self.assert_not_complete(p)
        self.assertTrue(any(i["status"] == "REQUIRED_EVIDENCE_UNAVAILABLE" for i in inv["issues"]))

    def test_type_confusion_symlink_and_outside_payload(self):
        secret = self.base / "outside-secret"
        secret.write_bytes(b"never copy this")
        runner = self.src / "usr/local/sbin/athena-alpha-m1-runner"
        runner.unlink(); runner.symlink_to(secret)
        p = self.run_collector()
        inv = self.assert_not_complete(p)
        self.assertIn("SYMLINK_NOT_FOLLOWED", {r["status"] for r in inv["results"]})
        captured = self.dest / "result/captured"
        self.assertFalse(captured.exists() and any(x.read_bytes() == secret.read_bytes()
                                                   for x in captured.iterdir()))

    def test_descriptor_replacements_and_content_mutation_are_reported(self):
        p = self.run_collector(MONAD_M34_TEST_REPLACE_BEFORE_OPEN="/usr/local/sbin")
        inv = self.assert_not_complete(p)
        self.assertIn("IDENTITY_CHANGED", {r["status"] for r in inv["results"]})
        shutil.rmtree(self.dest / "result")
        self.put("usr/local/sbin/athena-alpha-m1-runner", b"runner payload")
        p = self.run_collector(MONAD_M34_TEST_REPLACE_AFTER_OPEN="/usr/local/sbin/athena-alpha-m1-runner")
        inv = self.assert_not_complete(p)
        self.assertIn("PATH_REPLACED_DURING_ACQUISITION", {r["status"] for r in inv["results"]})
        shutil.rmtree(self.dest / "result")
        self.put("usr/local/sbin/athena-alpha-m1-runner", b"runner payload")
        p = self.run_collector(MONAD_M34_TEST_MUTATE_FILE="/usr/local/sbin/athena-alpha-m1-runner")
        inv = self.assert_not_complete(p)
        self.assertIn("CHANGED_DURING_ACQUISITION", {r["status"] for r in inv["results"]})

    def test_enumeration_error_does_not_report_complete(self):
        p = self.run_collector(MONAD_M34_TEST_MUTATE_DIRECTORY="/usr/local/sbin")
        inv = self.inventory()
        self.assertIn(inv["completion"], ("PARTIAL", "BLOCKED"))
        self.assertNotEqual(p.returncode, 0)
        self.assertTrue(inv["issues"])

    def test_statx_descriptor_mount_id_is_observed_and_failure_blocks(self):
        # Independent direct syscall probe against an ordinary open directory.
        libc = ctypes.CDLL(None, use_errno=True)
        fn = getattr(libc, "statx", None)
        self.assertIsNotNone(fn)
        fn.argtypes = (ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_uint, ctypes.c_void_p)
        fn.restype = ctypes.c_int
        fd = os.open(self.src, os.O_RDONLY | os.O_DIRECTORY)
        try:
            buf = ctypes.create_string_buffer(256)
            self.assertEqual(fn(fd, b"", 0x1000, 0x1000, buf), 0, os.strerror(ctypes.get_errno()))
            mask = ctypes.c_uint.from_buffer(buf, 0).value
            mount_id = ctypes.c_ulonglong.from_buffer(buf, 144).value
            self.assertTrue(mask & 0x1000)
            self.assertGreater(mount_id, 0)
        finally:
            os.close(fd)
        p = self.run_collector(MONAD_M34_TEST_MOUNT_ID_ERROR="/usr/local/sbin/athena-alpha-m1-runner")
        inv = self.assert_not_complete(p)
        self.assertEqual(inv["completion"], "BLOCKED")
        self.assertIn("MOUNT_ID_UNAVAILABLE", {r["status"] for r in inv["results"]})

    def test_mount_boundary_mismatch_and_unavailable_are_fail_closed(self):
        p = self.run_collector(MONAD_M34_TEST_BIND_MOUNT="/usr/local/sbin/athena-alpha-m1-runner")
        inv = self.assert_not_complete(p)
        self.assertEqual(inv["completion"], "PARTIAL")
        self.assertIn("MOUNT_BOUNDARY", {r["status"] for r in inv["results"]})
        shutil.rmtree(self.dest / "result")
        p = self.run_collector(MONAD_M34_TEST_MOUNT_ID_UNAVAILABLE="1")
        self.assertEqual(p.returncode, 2)
        self.assertFalse((self.dest / "result").exists())

    def test_exact_marker_allowlist_and_hostile_names(self):
        allowed = self.put(f"{BACKUP}/runner.absent", b"approved marker")
        hostile = self.put(f"{BACKUP}/private.absent", b"secret marker")
        self.put(f"{BACKUP}/..secret.absent", b"dot marker")
        p = self.run_collector()
        inv = self.assert_not_complete(p)
        self.assertIn("UNEXPECTED_BACKUP_MARKER", {i["status"] for i in inv["issues"]})
        payloads = [x.read_bytes() for x in (self.dest / "result/captured").iterdir()]
        self.assertIn(allowed.read_bytes(), payloads)
        self.assertNotIn(hostile.read_bytes(), payloads)
        self.assertNotIn(b"dot marker", payloads)

    def test_hostile_output_path_and_existing_destination_are_refused(self):
        p = self.run_collector("../escape")
        self.assertEqual(p.returncode, 2)
        self.assertFalse((self.base / "escape").exists())
        existing = self.dest / "occupied"
        existing.mkdir(); (existing / "keep").write_text("preserve")
        p = self.run_collector("occupied")
        self.assertEqual(p.returncode, 2)
        self.assertEqual((existing / "keep").read_text(), "preserve")

    def test_output_checksums_match_manifest_and_payloads(self):
        p = self.run_collector()
        self.assertEqual(p.returncode, 0, p.stderr)
        inv = self.inventory()
        self.assertEqual(inv["completion"], "COMPLETE")
        for line in (self.dest / "result/SHA256SUMS").read_text().splitlines():
            digest, name = line.split("  ", 1)
            self.assertEqual(hashlib.sha256((self.dest / "result" / name).read_bytes()).hexdigest(), digest)

    def test_unexpected_names_and_directory_children_remain_bounded(self):
        for idx in range(8001):
            (self.src / "root/athena-alpha-m1-stage" / f"entry-{idx:05d}").touch()
        p = self.run_collector()
        inv = self.inventory()
        self.assertNotEqual(inv["completion"], "COMPLETE")
        self.assertNotEqual(p.returncode, 0)
        self.assertLessEqual(inv["counts"]["enumerated"], 8003)
        self.assertTrue(any("LIMIT" in i["status"] or i["status"] == "ENUMERATION_ERROR"
                            for i in inv["issues"]))

    def test_controlled_interruption_is_manifested_blocked(self):
        p = self.run_collector(MONAD_M34_TEST_INTERRUPT_AFTER_FIRST_SCOPE="1")
        inv = self.inventory()
        self.assertEqual((p.returncode, inv["completion"]), (2, "BLOCKED"))
        self.assertIn("CONTROLLED_INTERRUPTION", {i["status"] for i in inv["issues"]})

    def test_embedded_python_and_shell_syntax(self):
        source = SCRIPT.read_text()
        embedded = source.split("<<'PY'\n", 1)[1].rsplit("\nPY", 1)[0]
        compile(embedded, str(SCRIPT), "exec")
        p = subprocess.run(["bash", "-n", str(SCRIPT)], capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
