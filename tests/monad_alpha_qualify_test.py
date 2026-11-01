#!/usr/bin/env python3
"""Nonprivileged unit checks for the local alpha qualification launcher."""
import contextlib
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from scripts import monad_alpha_qualify as launcher


class QualifierTests(unittest.TestCase):
    def test_launcher_refuses_run_without_separate_authorization_before_preflight(self):
        with mock.patch.object(launcher, "preflight") as preflight:
            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                code = launcher.main(["--run"])
        self.assertEqual(code, 2)
        self.assertIn("requires --authorize-e2e", stderr.getvalue())
        preflight.assert_not_called()

    def test_check_records_receipt_and_delegates_only_to_protected_runner(self):
        with tempfile.TemporaryDirectory() as parent:
            area = Path(parent) / "area"
            area.mkdir(mode=0o700)
            os.chmod(area, 0o700)
            fake = {"identity": {"dmi_product_name": "VirtualBox"},
                    "staging": {"sha256": {"bin/mpkg": "a" * 64}}}
            runner = mock.Mock(return_value={"status": "PASS", "vm_uuid": "verified"})
            with mock.patch.object(launcher, "preflight", return_value=fake), \
                    mock.patch.object(launcher, "validate_area", return_value=area), \
                    mock.patch.object(launcher, "check_privileged_runner", runner), \
                    mock.patch.object(launcher.subprocess, "run") as run:
                stdout = io.StringIO()
                with contextlib.redirect_stdout(stdout):
                    code = launcher.main(["--check", "--area", str(area)])
            self.assertEqual(code, 0)
            runner.assert_called_once_with(area, "preflight")
            run.assert_not_called()
            receipt = area / "preflight.json"
            self.assertTrue(receipt.is_file())
            self.assertEqual((area / "preflight.sha256.json").is_file(), True)
            self.assertEqual(receipt.stat().st_mode & 0o777, 0o600)

    def test_receipt_creation_is_exclusive_and_preserves_prior_evidence(self):
        with tempfile.TemporaryDirectory() as parent:
            path = Path(parent) / "receipt.json"
            path.write_text("historical\n")
            with self.assertRaises(FileExistsError):
                launcher.save_json(path, {"new": True})
            self.assertEqual(path.read_text(), "historical\n")

    def test_area_must_be_direct_child_of_var_tmp(self):
        with tempfile.TemporaryDirectory() as parent:
            with self.assertRaisesRegex(launcher.Blocked, "direct child of /var/tmp"):
                launcher.validate_area(Path(parent) / "area")

    def test_stage_hash_disagreement_fails_closed(self):
        with tempfile.TemporaryDirectory() as parent:
            stage = Path(parent) / "stage"
            (stage / "bin").mkdir(parents=True)
            (stage / "lib").mkdir()
            stage.chmod(0o700)
            (stage / "bin").chmod(0o700)
            (stage / "lib").chmod(0o700)
            binary = stage / "bin/mpkg"
            binary.write_text("test binary")
            binary.chmod(0o700)
            (stage / "lib/libathena_core.so").write_text("test library")
            with mock.patch.object(launcher, "STAGE", stage), \
                    mock.patch.dict(launcher.EXPECTED_SHA256,
                                    {"bin/mpkg": "0" * 64,
                                     "lib/libathena_core.so": "0" * 64}), \
                    self.assertRaisesRegex(launcher.Blocked, "SHA-256 mismatch"):
                launcher.check_stage()

    def test_group_writable_stage_directory_fails_closed(self):
        with tempfile.TemporaryDirectory() as parent:
            stage = Path(parent) / "stage"
            (stage / "bin").mkdir(parents=True)
            (stage / "lib").mkdir()
            stage.chmod(0o770)
            with mock.patch.object(launcher, "STAGE", stage), \
                    self.assertRaisesRegex(launcher.Blocked, "not group/world writable"):
                launcher.check_stage()

    def test_read_only_var_tmp_filesystem_fails_closed(self):
        result = launcher.subprocess.CompletedProcess(
            args=[], returncode=0, stdout="/ /dev/sda2 ext4 ro,nosuid,nodev\n", stderr="")
        with mock.patch.object(launcher.subprocess, "run", return_value=result), \
                self.assertRaisesRegex(launcher.Blocked, "not mounted read-write"):
            launcher.check_filesystem()

    def test_staging_artifact_hash_matches_documented_reconciliation(self):
        self.assertEqual(
            "bd20e79efe91a914a811f06eb3c3cfdb157ff7e525c7854415c89732b24e0e8f",
            launcher.EXPECTED_SHA256["bin/mpkg"])

    def test_privileged_harness_authorization_is_not_accepted_as_launcher_authorization(self):
        with mock.patch.object(launcher, "preflight") as preflight:
            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                code = launcher.main(["--run", "--authorize-e2e", "RUN-IN-APPROVED-DISPOSABLE-VM"])
        self.assertEqual(code, 2)
        self.assertIn("requires --authorize-e2e", stderr.getvalue())
        preflight.assert_not_called()

    def test_user_supplied_identity_and_isolation_claims_are_rejected(self):
        with mock.patch.object(launcher, "preflight") as preflight:
            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                with self.assertRaises(SystemExit) as exit_status:
                    launcher.main(["--run", "--authorize-e2e", launcher.RUN_AUTH,
                                   "--original-vm-uuid", "00000000-0000-0000-0000-000000000001",
                                   "--confirm-clone-isolation", "anything"])
        self.assertEqual(exit_status.exception.code, 2)
        self.assertIn("unrecognized arguments", stderr.getvalue())
        preflight.assert_not_called()

    def test_preflight_does_not_require_capability_in_client_process(self):
        with mock.patch.object(launcher.os, "geteuid", return_value=1000), \
                mock.patch.object(launcher, "check_filesystem", return_value={}), \
                mock.patch.object(launcher, "check_stage", return_value={}):
            with mock.patch.dict(launcher.os.environ, {"PATH": "/usr/bin:/bin"}, clear=True):
                self.assertEqual(launcher.preflight(), {"filesystem": {}, "staging": {}})


if __name__ == "__main__":
    unittest.main(verbosity=2)
