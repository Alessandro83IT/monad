#!/usr/bin/env python3
"""Unprivileged adversarial tests for gateway primitives; no systemd or sudo."""

import hashlib
import importlib.util
import json
import os
import pathlib
import subprocess
import tempfile
import threading
import time
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("gateway_support", ROOT / "qualification/gateway_support.py")
gateway = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gateway)


class GatewayHardeningTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="monad-gateway-")
        self.root = pathlib.Path(self.temp.name)
        self.source = self.root / "source"
        self.stage = self.root / "stage"
        self.source.mkdir(mode=0o700)
        self.stage.mkdir(mode=0o700)
        self.source_fd = os.open(self.source, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        self.stage_fd = os.open(self.stage, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        self.owner = os.geteuid()

    def tearDown(self):
        os.close(self.source_fd)
        os.close(self.stage_fd)
        self.temp.cleanup()

    def make_artifact(self, name="test.bin", data=b"pinned test payload", executable=True):
        path = self.source / name
        path.write_bytes(data)
        path.chmod(0o500 if executable else 0o400)
        return hashlib.sha256(data).hexdigest(), executable

    def stage_one(self, name="test.bin", digest=None, executable=True, callback=None):
        if digest is None:
            digest, executable = self.make_artifact(name, executable=executable)
        return gateway.stage_artifacts(
            self.source_fd, self.stage_fd, {name: (digest, executable)}, "case-001",
            source_owner=self.owner, stage_owner=self.owner, on_source_open=callback)

    def test_stage_survives_concurrent_path_replacement_and_uses_pinned_inode(self):
        original_hash, executable = self.make_artifact()
        replacement = self.root / "replacement"
        replacement.write_bytes(b"attacker payload")
        replacement.chmod(0o500)
        ready, release = threading.Event(), threading.Event()
        errors = []

        def swap_after_open(name, _fd):
            ready.set()
            if not release.wait(3):
                errors.append("race test synchronization timeout")

        def attacker():
            self.assertTrue(ready.wait(3))
            (self.source / "test.bin").unlink()
            os.symlink(replacement, self.source / "test.bin")
            release.set()

        thread = threading.Thread(target=attacker)
        thread.start()
        with self.assertRaisesRegex(gateway.GatewayError, "changed while staging"):
            self.stage_one(digest=original_hash, executable=executable, callback=swap_after_open)
        thread.join(3)
        self.assertFalse(thread.is_alive())
        self.assertFalse(errors)
        self.assertEqual(list(self.stage.iterdir()), [])
        self.assertTrue((self.source / "test.bin").is_symlink())

    def test_symlink_source_and_traversal_names_fail_closed(self):
        digest, executable = self.make_artifact()
        (self.source / "link.bin").symlink_to(self.source / "test.bin")
        with self.assertRaises(OSError):
            gateway.stage_artifacts(self.source_fd, self.stage_fd,
                                    {"link.bin": (digest, executable)}, "case-link",
                                    source_owner=self.owner, stage_owner=self.owner)
        for hostile in ("../escape", "/tmp/payload", "..", "dir/file"):
            with self.subTest(name=hostile), self.assertRaises(gateway.GatewayError):
                gateway.stage_artifacts(self.source_fd, self.stage_fd,
                                        {hostile: (digest, executable)}, "case-hostile",
                                        source_owner=self.owner, stage_owner=self.owner)
        self.assertEqual(list(self.stage.iterdir()), [])

    def test_wrong_hash_owner_permissions_and_special_files_are_rejected(self):
        digest, executable = self.make_artifact()
        with self.assertRaisesRegex(gateway.GatewayError, "hash mismatch"):
            gateway.stage_artifacts(self.source_fd, self.stage_fd,
                                    {"test.bin": ("0" * 64, executable)}, "case-hash",
                                    source_owner=self.owner, stage_owner=self.owner)
        (self.source / "test.bin").chmod(0o520)
        with self.assertRaisesRegex(gateway.GatewayError, "permissions"):
            gateway.stage_artifacts(self.source_fd, self.stage_fd,
                                    {"test.bin": (digest, executable)}, "case-mode",
                                    source_owner=self.owner, stage_owner=self.owner)
        with self.assertRaisesRegex(gateway.GatewayError, "owner"):
            gateway.stage_artifacts(self.source_fd, self.stage_fd,
                                    {"test.bin": (digest, executable)}, "case-owner",
                                    source_owner=0, stage_owner=self.owner)
        fifo = self.source / "fifo"
        os.mkfifo(fifo, 0o600)
        with self.assertRaises(gateway.GatewayError):
            gateway.stage_artifacts(self.source_fd, self.stage_fd,
                                    {"fifo": (digest, False)}, "case-fifo",
                                    source_owner=self.owner, stage_owner=self.owner)

    def test_source_mutation_while_copying_is_detected_and_partial_stage_removed(self):
        digest, executable = self.make_artifact(data=b"A" * (4 * 1024 * 1024))
        started = threading.Event()

        def mutate(_name, fd):
            started.set()
            path = self.source / "test.bin"
            path.chmod(0o600)
            mutator = os.open(path, os.O_WRONLY)
            os.pwrite(mutator, b"B", 0)
            os.close(mutator)
            path.chmod(0o500)

        with self.assertRaises(gateway.GatewayError):
            gateway.stage_artifacts(self.source_fd, self.stage_fd,
                                    {"test.bin": (digest, executable)}, "case-mutated",
                                    source_owner=self.owner, stage_owner=self.owner, on_source_open=mutate)
        self.assertTrue(started.is_set())
        self.assertEqual(list(self.stage.iterdir()), [])

    def test_interrupted_staging_never_publishes_partial_tree(self):
        digest, executable = self.make_artifact()
        def interrupt(_name, _fd):
            raise KeyboardInterrupt("simulated staging interruption")
        with self.assertRaises(KeyboardInterrupt):
            gateway.stage_artifacts(self.source_fd, self.stage_fd,
                                    {"test.bin": (digest, executable)}, "case-interrupt",
                                    source_owner=self.owner, stage_owner=self.owner,
                                    on_source_open=interrupt)
        self.assertEqual(list(self.stage.iterdir()), [])

    def test_complete_stage_is_atomic_read_only_and_has_identity_manifest(self):
        digest, executable = self.make_artifact()
        run_id, manifest = self.stage_one(digest=digest, executable=executable)
        directory = self.stage / run_id
        self.assertEqual(directory.stat().st_mode & 0o777, 0o500)
        self.assertEqual((directory / "test.bin").stat().st_mode & 0o777, 0o500)
        record = json.loads((directory / "manifest.json").read_text())
        self.assertEqual(record, manifest)
        self.assertEqual(record["artifacts"]["test.bin"]["sha256"], digest)
        self.assertEqual(record["artifacts"]["test.bin"]["stage_ino"], (directory / "test.bin").stat().st_ino)
        self.assertEqual(hashlib.sha256((directory / "manifest.json").read_bytes()).hexdigest(),
                         hashlib.sha256(json.dumps(record, sort_keys=True, separators=(",", ":")).encode() + b"\n").hexdigest())

    def test_unit_state_requires_inactive_zero_pid_and_empty_cgroup(self):
        base = {"active_state": "inactive", "main_pid": 0, "cgroup_populated": False}
        self.assertTrue(gateway.terminal_unit_state(base))
        for changed in ({"active_state": "active"}, {"main_pid": 9}, {"cgroup_populated": True},
                        {"cgroup_populated": None}):
            state = base | changed
            with self.subTest(state=state):
                self.assertFalse(gateway.terminal_unit_state(state))

    def test_removed_cgroup_requires_successful_systemd_run_completion(self):
        removed = {"active_state": "inactive", "main_pid": 0,
                   "cgroup_populated": None, "result": "success"}
        self.assertFalse(gateway.terminal_unit_state(removed))
        self.assertTrue(gateway.terminal_unit_state(removed, allow_removed_after_success=True))
        for changed in ({"active_state": "failed", "result": "exit-code"},
                        {"active_state": "active", "result": "success"},
                        {"main_pid": 15, "result": "success"}):
            self.assertFalse(gateway.terminal_unit_state(
                removed | changed, allow_removed_after_success=True))

    def test_wait_accepts_disappeared_cgroup_only_for_successful_client(self):
        removed = {"active_state": "inactive", "main_pid": 0,
                   "cgroup_populated": None, "result": "success"}
        result = gateway.wait_for_unit_termination(
            lambda: removed, lambda: self.fail("terminal successful unit must not be stopped"),
            deadline=time.monotonic() + 1, clock=time.monotonic, sleep=lambda _: None,
            allow_removed_after_success=True)
        self.assertTrue(result["termination_confirmed"])
        with self.assertRaisesRegex(gateway.GatewayError, "termination is uncertain"):
            gateway.wait_for_unit_termination(
                lambda: removed, lambda: None,
                deadline=time.monotonic() + 0.02, clock=time.monotonic,
                sleep=lambda _: time.sleep(0.001))

    def test_blocked_unit_requests_stop_then_waits_for_confirmation(self):
        states = iter([
            {"active_state": "active", "main_pid": 123, "cgroup_populated": True},
            {"active_state": "deactivating", "main_pid": 123, "cgroup_populated": True},
            {"active_state": "inactive", "main_pid": 0, "cgroup_populated": False},
        ])
        stop_calls = []
        result = gateway.wait_for_unit_termination(
            lambda: next(states), lambda: stop_calls.append("stop"),
            deadline=time.monotonic() + 1, clock=time.monotonic, sleep=lambda _: None)
        self.assertTrue(result["termination_confirmed"])
        self.assertTrue(result["stop_requested"])
        self.assertEqual(stop_calls, ["stop"])

    def test_failed_stop_request_and_unknown_state_fail_closed(self):
        state = {"active_state": "deactivating", "main_pid": 44, "cgroup_populated": True}
        with self.assertRaisesRegex(RuntimeError, "stop backend unavailable"):
            gateway.wait_for_unit_termination(lambda: state,
                                              lambda: (_ for _ in ()).throw(RuntimeError("stop backend unavailable")),
                                              deadline=time.monotonic() + 1, clock=time.monotonic,
                                              sleep=lambda _: None)

    def test_still_active_unit_returns_uncertain_instead_of_success(self):
        state = {"active_state": "active", "main_pid": 123, "cgroup_populated": True}
        with self.assertRaisesRegex(gateway.GatewayError, "termination is uncertain"):
            gateway.wait_for_unit_termination(lambda: state, lambda: None,
                                              deadline=0.02, clock=time.monotonic,
                                              sleep=lambda _: time.sleep(0.001))

    def test_blocked_systemd_client_is_killed_and_reaped_or_reported_uncertain(self):
        class BlockedClient:
            def __init__(self, reapable):
                self.reapable = reapable
                self.killed = False

            def wait(self, timeout):
                if self.killed and self.reapable:
                    return -9
                raise subprocess.TimeoutExpired("systemd-run", timeout)

            def kill(self):
                self.killed = True

        client = BlockedClient(reapable=True)
        self.assertTrue(gateway.terminate_client_process(client, wait_timeout=0.01, kill_timeout=0.01))
        self.assertTrue(client.killed)
        stuck = BlockedClient(reapable=False)
        self.assertFalse(gateway.terminate_client_process(stuck, wait_timeout=0.01, kill_timeout=0.01))
        self.assertTrue(stuck.killed)

    def test_report_output_and_backup_manifest_integrity_reject_tampering(self):
        (self.root / "output.out").write_bytes(b"captured output")
        (self.root / "report.json").write_text(json.dumps({
            "output_sha256": hashlib.sha256(b"captured output").hexdigest(),
            "output_bytes": len(b"captured output")}))
        root_fd = os.open(self.root, os.O_RDONLY | os.O_DIRECTORY)
        try:
            verified = gateway.verify_report(root_fd, "report.json", "output.out")
            self.assertEqual(verified["output_sha256"], hashlib.sha256(b"captured output").hexdigest())
            manifest_name, _ = gateway.write_evidence_manifest(root_fd, "report.json", "output.out", "test-run")
            self.assertEqual(gateway.verify_evidence_manifest(root_fd, manifest_name)["output_sha256"], verified["output_sha256"])
            (self.root / "output.out").write_bytes(b"altered")
            with self.assertRaisesRegex(gateway.GatewayError, "integrity mismatch"):
                gateway.verify_evidence_manifest(root_fd, manifest_name)
            (self.root / "output.out").write_bytes(b"captured output")
            (self.root / "report.json").write_text(json.dumps({"output_sha256": "0" * 64,
                                                                  "output_bytes": len(b"captured output")}))
            with self.assertRaisesRegex(gateway.GatewayError, "integrity mismatch"):
                gateway.verify_evidence_manifest(root_fd, manifest_name)

            backup_dir = self.root / "backup"
            backup_dir.mkdir()
            backup_data = b"previous administrative state"
            (backup_dir / "runner.backup").write_bytes(backup_data)
            (backup_dir / "runner.backup").chmod(0o600)
            manifest = {"entries": {"runner.backup": {
                "sha256": hashlib.sha256(backup_data).hexdigest(), "size": len(backup_data),
                "uid": os.geteuid(), "gid": os.getegid(), "mode": 0o600, "type": "regular"}}}
            (backup_dir / "manifest.json").write_text(json.dumps(manifest))
            backup_fd = os.open(backup_dir, os.O_RDONLY | os.O_DIRECTORY)
            self.assertIn("runner.backup", gateway.validate_backup_manifest(backup_fd))
            target_dir = self.root / "targets"
            target_dir.mkdir()
            (target_dir / "runner.backup").write_bytes(b"old target")
            target_fd = os.open(target_dir, os.O_RDONLY | os.O_DIRECTORY)
            gateway.restore_backup_file(backup_fd, target_fd, "runner.backup")
            self.assertEqual((target_dir / "runner.backup").read_bytes(), backup_data)
            (backup_dir / "runner.backup").write_bytes(b"tampered")
            with self.assertRaisesRegex(gateway.GatewayError, "backup metadata or hash mismatch"):
                gateway.validate_backup_manifest(backup_fd)
            (target_dir / "runner.backup").write_bytes(b"sentinel")
            with self.assertRaisesRegex(gateway.GatewayError, "backup metadata or hash mismatch"):
                gateway.restore_backup_file(backup_fd, target_fd, "runner.backup")
            self.assertEqual((target_dir / "runner.backup").read_bytes(), b"sentinel")
            os.close(target_fd)
            os.close(backup_fd)
        finally:
            os.close(root_fd)

    def test_unapproved_operation_and_unknown_arguments_are_not_runner_cases(self):
        allowed = {"case_a": ("binary", "mode")}
        self.assertEqual(gateway.authorize_case("case_a", allowed), ("binary", "mode"))
        for denied in (None, "", "../case_a", "case_a extra", "rollback", "install", "case_b"):
            with self.subTest(case=denied), self.assertRaises(gateway.GatewayError):
                gateway.authorize_case(denied, allowed)


if __name__ == "__main__":
    unittest.main(verbosity=2)
