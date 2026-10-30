#!/usr/bin/env python3
"""Synthetic-only tests for the M28 metadata acquisition entry point."""
import json
import hashlib
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "qualification/m28-readonly-acquisition.sh"


class AcquisitionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="m28-test-")
        self.root = pathlib.Path(self.tmp.name)
        self.fixture = self.root / "fixture"
        self.fixture.mkdir()
        for rel in ("usr/local/sbin", "etc/sudoers.d", "var/log/athena-alpha-m1",
                    "var/lib/athena-alpha-m1/backups/tx1", "run/athena-alpha-m1",
                    "root/athena-alpha-m1-stage", "opt/athena-qualification/build",
                    "var/tmp/athena-d67-ext4"):
            (self.fixture / rel).mkdir(parents=True, exist_ok=True)
        (self.fixture / "usr/local/sbin/athena-alpha-m1-runner").write_text("synthetic executable")
        (self.fixture / "etc/sudoers.d/athena-alpha-m1").write_text("synthetic policy")
        (self.fixture / "var/lib/athena-alpha-m1/backups/tx1/install-started").write_text("marker")
        self.env = os.environ.copy()
        self.env["MONAD_M28_TEST_ROOT"] = str(self.fixture)

    def tearDown(self):
        self.tmp.cleanup()

    def run_acq(self, output):
        return subprocess.run([str(SCRIPT), str(output)], env=self.env,
                              text=True, capture_output=True, check=False)

    def test_expected_paths_missing_and_sensitive_contents(self):
        out = self.root / "evidence"
        p = self.run_acq(out)
        self.assertEqual(p.returncode, 0, p.stderr)
        doc = json.loads((out / "inventory.json").read_text())
        by_path = {r["path"]: r for r in doc["results"]}
        self.assertIn("/var/lib/athena-alpha-m1/backups", by_path)
        self.assertIn("/var/lib/athena-alpha-m1/backups/tx1/install-started", by_path)
        self.assertEqual(by_path["/var/lib/athena-alpha-m1/systemd-canary.json"]["status"], "absent")
        self.assertNotIn("sha256", by_path["/etc/sudoers.d/athena-alpha-m1"])
        self.assertEqual(doc["completion"], "COMPLETE")
        self.assertTrue((out / "SHA256SUMS").is_file())
        expected = hashlib.sha256((out / "inventory.json").read_bytes()).hexdigest()
        self.assertEqual((out / "SHA256SUMS").read_text(), expected + "  inventory.json\n")

    def test_symlink_is_not_followed_and_is_incomplete(self):
        target = self.fixture / "var/tmp/athena-d67-ext4/escape"
        target.symlink_to("/etc")
        out = self.root / "evidence"
        p = self.run_acq(out)
        self.assertEqual(p.returncode, 1)
        doc = json.loads((out / "inventory.json").read_text())
        link = next(x for x in doc["results"] if x["path"].endswith("/escape"))
        self.assertEqual(link["status"], "symlink-not-followed")
        self.assertEqual(doc["completion"], "PARTIAL")

    def test_inaccessible_path_is_partial_when_permissions_apply(self):
        target = self.fixture / "var/log/athena-alpha-m1"
        target.chmod(0)
        out = self.root / "evidence"
        p = self.run_acq(out)
        target.chmod(0o755)
        if os.access(target, os.R_OK):
            self.skipTest("sandbox identity can still read mode-000 fixture")
        self.assertEqual(p.returncode, 1)
        self.assertEqual(json.loads((out / "inventory.json").read_text())["completion"], "PARTIAL")

    def test_output_collision_and_failure_are_nonzero(self):
        existing = self.root / "exists"
        existing.mkdir()
        self.assertEqual(self.run_acq(existing).returncode, 2)
        # A regular file cannot be used as the parent for the new output directory.
        blocker = self.root / "blocker"
        blocker.write_text("x")
        self.assertEqual(self.run_acq(blocker / "evidence").returncode, 2)


if __name__ == "__main__":
    unittest.main()
