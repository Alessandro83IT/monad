#!/usr/bin/env python3
"""Nonprivileged checks for the alpha E2E harness's early guards."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/monad_alpha_e2e.py"
AUTH = "RUN-IN-APPROVED-DISPOSABLE-VM"


class HarnessGuardTests(unittest.TestCase):
    def invoke(self, token, area):
        env = {"PATH": "/usr/bin:/bin"}
        return subprocess.run(
            [sys.executable, str(SCRIPT), "--authorize", token,
             "--mpkg", "/does/not/need/to/exist", "--area", str(area)],
            env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, check=False)

    def test_bad_authorization_stops_before_filesystem_or_privilege_checks(self):
        with tempfile.TemporaryDirectory() as parent:
            area = Path(parent) / "area"
            result = self.invoke("wrong-token", area)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("authorization token mismatch", result.stderr)
            self.assertFalse(area.exists())

    def test_parent_traversal_rejected_before_privilege_check(self):
        result = self.invoke(AUTH, "/var/tmp/../tmp/monad-alpha-guard-test")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("parent traversal", result.stderr)

    def test_symlink_component_rejected_before_privilege_check(self):
        with tempfile.TemporaryDirectory() as parent:
            link = Path(parent) / "link"
            link.symlink_to(parent, target_is_directory=True)
            result = self.invoke(AUTH, link / "area")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("must not contain symlinks", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
