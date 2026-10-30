#!/usr/bin/env python3
"""Synthetic-only tests for M28.1. No privileged paths are touched."""
import json
import os
import pathlib
import socket
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "qualification/m28-1-readonly-acquisition.sh"


class M281Tests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="m28-1-test-")
        self.base = pathlib.Path(self.tmp.name)
        self.fixture = self.base / "fixture"
        self.fixture.mkdir(mode=0o700)
        for rel in ("usr/local/sbin", "etc/sudoers.d", "var/log/athena-alpha-m1",
                    "var/lib/athena-alpha-m1/backups/tx1", "run/athena-alpha-m1",
                    "root/athena-alpha-m1-stage", "opt/athena-qualification/build",
                    "var/tmp/athena-d67-ext4"):
            (self.fixture / rel).mkdir(parents=True, exist_ok=True)
        (self.fixture / "usr/local/sbin/athena-alpha-m1-runner").write_text("synthetic runner")
        (self.fixture / "var/lib/athena-alpha-m1/backups/tx1/marker").write_text("transaction")
        self.env = os.environ.copy()
        self.env["MONAD_M28_1_TEST_ROOT"] = str(self.fixture)

    def tearDown(self):
        self.tmp.cleanup()

    def run_acq(self, name="evidence", **env):
        output = self.base / name
        e = self.env.copy()
        e.update({k: str(v) for k, v in env.items()})
        p = subprocess.run(["bash", str(SCRIPT), str(output)], cwd=ROOT, env=e,
                           text=True, capture_output=True, timeout=10)
        doc = None
        if (output / "inventory.json").exists():
            doc = json.loads((output / "inventory.json").read_text())
        return p, output, doc

    def test_01_valid_synthetic_acquisition(self):
        p, out, doc = self.run_acq()
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(doc["completion"], "COMPLETE")
        self.assertEqual(doc["identity"]["machine_id"], "synthetic-machine-id")
        self.assertTrue((out / "SHA256SUMS").exists())
        self.assertEqual((out.stat().st_mode & 0o777), 0o700)
        self.assertEqual(((out / "inventory.json").stat().st_mode & 0o777), 0o600)

    def test_02_missing_optional_paths_are_absent(self):
        p, _, doc = self.run_acq()
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("ABSENT", {r["status"] for r in doc["results"]})

    def test_03_symlinked_root(self):
        target = self.fixture / "var/log/athena-alpha-m1"
        target.rmdir(); target.symlink_to(self.fixture / "run/athena-alpha-m1")
        p, _, doc = self.run_acq()
        self.assertEqual(p.returncode, 1)
        self.assertTrue(any(r["path"] == "/var/log/athena-alpha-m1" and r["status"] == "SYMLINK_NOT_FOLLOWED" for r in doc["results"]))

    def test_04_symlinked_descendant(self):
        (self.fixture / "var/tmp/athena-d67-ext4/escape").symlink_to("/etc")
        p, _, doc = self.run_acq()
        self.assertEqual(p.returncode, 1)
        self.assertIn("SYMLINK_NOT_FOLLOWED", {r["status"] for r in doc["results"]})

    def test_05_fifo_is_never_opened(self):
        os.mkfifo(self.fixture / "var/tmp/athena-d67-ext4/pipe")
        p, _, doc = self.run_acq()
        self.assertEqual(p.returncode, 1)
        row = next(r for r in doc["results"] if r["path"].endswith("/pipe"))
        self.assertEqual(row["status"], "ERROR")
        self.assertEqual(p.returncode, 1)

    def test_06_unix_socket_is_never_opened(self):
        path = str(self.fixture / "var/tmp/athena-d67-ext4/socket")
        sock = socket.socket(socket.AF_UNIX)
        try: sock.bind(path)
        except PermissionError as exc:
            sock.close(); self.skipTest("sandbox denies AF_UNIX bind in synthetic fixture: " + str(exc))
        try:
            p, _, doc = self.run_acq()
            self.assertEqual(p.returncode, 1)
            self.assertEqual(next(r for r in doc["results"] if r["path"].endswith("/socket"))["status"], "ERROR")
        finally: sock.close()

    def test_07_permission_denied_traversal(self):
        path = self.fixture / "var/log/athena-alpha-m1"
        path.chmod(0)
        try:
            p, _, doc = self.run_acq()
        finally: path.chmod(0o755)
        if os.access(path, os.R_OK): self.skipTest("effective test identity bypasses mode-000 directory permissions")
        self.assertEqual(p.returncode, 1); self.assertEqual(doc["completion"], "PARTIAL")

    def test_08_existing_output_rejected(self):
        (self.base / "existing").mkdir()
        p = subprocess.run(["bash", str(SCRIPT), str(self.base / "existing")], cwd=ROOT, env=self.env, capture_output=True)
        self.assertEqual(p.returncode, 2)

    def test_09_symlinked_output_parent_rejected(self):
        link = self.base / "link"; link.symlink_to(self.base, target_is_directory=True)
        p = subprocess.run(["bash", str(SCRIPT), str(link / "evidence")], cwd=ROOT, env=self.env, capture_output=True)
        self.assertEqual(p.returncode, 2)

    def test_10_unsafe_output_parent_permissions_rejected(self):
        parent = self.base / "writable"; parent.mkdir(mode=0o777); parent.chmod(0o777)
        p = subprocess.run(["bash", str(SCRIPT), str(parent / "evidence")], cwd=ROOT, env=self.env, capture_output=True)
        self.assertEqual(p.returncode, 2)

    def test_11_directory_mutation_detected(self):
        p, _, doc = self.run_acq(MONAD_M28_1_TEST_MUTATE_DIRECTORY="/var/log/athena-alpha-m1")
        self.assertEqual(p.returncode, 1)
        self.assertIn("CHANGED", {r["status"] for r in doc["results"]})

    def test_12_file_mutation_during_hash_detected(self):
        target = self.fixture / "var/tmp/athena-d67-ext4/data"
        target.write_bytes(b"a" * 4096)
        p, _, doc = self.run_acq(MONAD_M28_1_TEST_MUTATE_FILE="/var/tmp/athena-d67-ext4/data")
        self.assertEqual(p.returncode, 1)
        self.assertEqual(next(r for r in doc["results"] if r["path"].endswith("/data"))["status"], "CHANGED")

    def test_13_entry_count_limit(self):
        p, _, doc = self.run_acq(MONAD_M28_1_LIMIT_ENTRIES="2")
        self.assertEqual(p.returncode, 1); self.assertTrue(any("limit_exceeded" in r for r in doc["results"]))

    def test_14_depth_limit(self):
        p, _, doc = self.run_acq(MONAD_M28_1_LIMIT_DEPTH="1")
        self.assertEqual(p.returncode, 1); self.assertTrue(any(r.get("limit_exceeded") == "depth" for r in doc["results"]))

    def test_15_individual_file_size_limit(self):
        (self.fixture / "var/tmp/athena-d67-ext4/large").write_bytes(b"x" * 100)
        p, _, doc = self.run_acq(MONAD_M28_1_LIMIT_FILE="32")
        self.assertEqual(p.returncode, 1); self.assertTrue(any(r.get("limit_exceeded") == "file_size" for r in doc["results"]))

    def test_16_total_hashing_limit(self):
        (self.fixture / "var/tmp/athena-d67-ext4/data").write_bytes(b"x" * 100)
        p, _, doc = self.run_acq(MONAD_M28_1_LIMIT_TOTAL="32")
        self.assertEqual(p.returncode, 1); self.assertTrue(any(r.get("limit_exceeded") == "total_bytes" for r in doc["results"]))

    def test_17_output_size_limit(self):
        (self.fixture / "var/tmp/athena-d67-ext4" / ("x" * 80)).write_text("x")
        p, _, doc = self.run_acq(MONAD_M28_1_LIMIT_OUTPUT="4096")
        self.assertEqual(p.returncode, 1); self.assertIn("output_size", str(doc["issues"]))

    def test_18_identity_mismatch_rejected(self):
        p = subprocess.run(["bash", str(SCRIPT), str(self.base / "production")], cwd=ROOT,
                           env={k:v for k,v in self.env.items() if k != "MONAD_M28_1_TEST_ROOT"}, capture_output=True)
        self.assertEqual(p.returncode, 2)

    def test_19_production_mode_rejected_outside_target_vm(self):
        env = {k:v for k,v in self.env.items() if k != "MONAD_M28_1_TEST_ROOT"}
        env["MONAD_M28_1_LIMIT_ENTRIES"] = "2"
        p = subprocess.run(["bash", str(SCRIPT), str(self.base / "production")], cwd=ROOT,
                           env=env, capture_output=True)
        self.assertEqual(p.returncode, 2)

    def test_20_synthetic_mode_rejected_as_root(self):
        if os.geteuid() != 0: self.skipTest("test runner is non-root; root refusal is guarded directly in script")
        p, _, _ = self.run_acq()
        self.assertEqual(p.returncode, 2)


if __name__ == "__main__": unittest.main(verbosity=2)
