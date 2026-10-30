#!/usr/bin/env python3
"""Rootless, isolated transaction tests for the Alpha M1 admin installer."""

import hashlib
import json
import os
import pathlib
import signal
import subprocess
import tempfile
import time
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
INSTALLER = ROOT / "qualification/install-athena-alpha-m1-admin.sh"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class TransactionFixture:
    def __init__(self):
        self.temp = tempfile.TemporaryDirectory(prefix="alpha-m1-transaction-")
        self.root = pathlib.Path(self.temp.name)
        os.chmod(self.root, 0o700)
        self.stage = self.root / "stage"
        self.runner_dir = self.root / "usr/local/sbin"
        self.sudoers_dir = self.root / "etc/sudoers.d"
        self.logdir = self.root / "var/log/athena-alpha-m1"
        self.state_root = self.root / "var/lib/athena-alpha-m1"
        self.backups = self.state_root / "backups"
        for directory in (self.stage, self.runner_dir, self.sudoers_dir, self.logdir,
                          self.state_root / "ext4-fixture", self.backups):
            directory.mkdir(parents=True, exist_ok=True)
        for directory in sorted(self.root.rglob("*"), key=lambda path: len(path.parts), reverse=True):
            if directory.is_dir():
                directory.chmod(0o700)
        for name, content in (("athena-alpha-m1-runner.py", b"candidate runner\n"),
                              ("gateway_support.py", b"candidate support\n"),
                              ("athena-alpha-m1-sudoers", b"candidate sudoers\n")):
            source = self.stage / name
            source.write_bytes(content)
            source.chmod(0o600)
        (self.state_root / "systemd-canary.json").write_bytes(b"receipt evidence\n")
        self.transaction = "txn-session34-a"
        self.backup = self.backups / self.transaction

    def close(self):
        self.temp.cleanup()

    def env(self, **extra):
        result = os.environ.copy()
        result["MONAD_ALPHA_M1_TEST_ROOT"] = str(self.root)
        result.pop("MONAD_ALPHA_M1_FAULT_POINT", None)
        result.pop("MONAD_ALPHA_M1_TEST_HOLD_LOCK", None)
        result.pop("MONAD_ALPHA_M1_TEST_PAUSE_AT", None)
        result.update({key: str(value) for key, value in extra.items()})
        return result

    def invoke(self, action="install", transaction=None, extra=None, approval=None, timeout=10):
        transaction = transaction or self.transaction
        backup = self.backups / transaction
        args = ["bash", str(INSTALLER), action, str(backup)]
        if action == "rollback":
            args += ["--transaction-id", transaction, "--approval-file", str(approval),
                     "--approval-sha256", digest(pathlib.Path(approval))]
        return subprocess.run(args, env=self.env(**(extra or {})), text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)

    def seed_targets(self):
        for path, content in ((self.runner_dir / "athena-alpha-m1-runner", b"old runner\n"),
                              (self.runner_dir / "gateway_support.py", b"old support\n"),
                              (self.sudoers_dir / "athena-alpha-m1", b"old sudoers\n")):
            path.write_bytes(content)
            path.chmod(0o600)

    def evidence_snapshot(self):
        paths = [self.state_root / "systemd-canary.json", self.backup]
        result = {}
        for base in paths:
            if not base.exists():
                result[str(base.relative_to(self.root))] = None
                continue
            for item in ([base] if base.is_file() else sorted(base.rglob("*"))):
                if item.is_file():
                    st = item.stat()
                    result[str(item.relative_to(self.root))] = (digest(item), st.st_mode & 0o777)
                elif item.is_dir():
                    result[str(item.relative_to(self.root)) + "/"] = ("directory", item.stat().st_mode & 0o777)
        return result

    def approval(self, *, transaction=None, alter=None):
        transaction = transaction or self.transaction
        state = self.backups / transaction
        transaction_raw = (state / "transaction.json").read_bytes()
        manifest_raw = (state / "backup-manifest.json").read_bytes()
        record = {
            "schema": 1,
            "action": "rollback",
            "transaction_id": transaction,
            "backup_manifest_sha256": hashlib.sha256(manifest_raw).hexdigest(),
            "transaction_sha256": hashlib.sha256(transaction_raw).hexdigest(),
        }
        if alter:
            record.update(alter)
        approval = self.root / "rollback-approval.json"
        approval.write_text(json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n")
        approval.chmod(0o600)
        return approval

    def rewrite_transaction(self, mutate):
        path = self.backup / "transaction.json"
        record = json.loads(path.read_bytes())
        mutate(record)
        path.write_text(json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n")
        path.chmod(0o600)
        return path


class InstallerTransactionTest(unittest.TestCase):
    def setUp(self):
        self.fx = TransactionFixture()

    def tearDown(self):
        self.fx.close()

    def test_successful_install_writes_canonical_transaction_and_completion(self):
        self.fx.seed_targets()
        result = self.fx.invoke()
        self.assertEqual(0, result.returncode, result.stderr)
        state = self.fx.backup
        self.assertTrue((state / "install-started").is_file())
        self.assertTrue((state / "install-complete").is_file())
        self.assertFalse((state / "install-partial").exists())
        record_raw = (state / "transaction.json").read_bytes()
        record = json.loads(record_raw)
        self.assertEqual(record_raw, json.dumps(record, sort_keys=True, separators=(",", ":")).encode() + b"\n")
        self.assertEqual(self.fx.transaction, record["transaction_id"])
        self.assertEqual(3, len(record["destinations"]))

    def test_pre_marker_error_preserves_targets_and_refuses_transaction_reuse(self):
        self.fx.seed_targets()
        before = {p: digest(p) for p in (self.fx.runner_dir / "athena-alpha-m1-runner",
                                         self.fx.runner_dir / "gateway_support.py",
                                         self.fx.sudoers_dir / "athena-alpha-m1")}
        result = self.fx.invoke(extra={"MONAD_ALPHA_M1_FAULT_POINT": "before-install-started"})
        self.assertNotEqual(0, result.returncode)
        self.assertFalse((self.fx.backup / "install-started").exists())
        self.assertEqual(before, {p: digest(p) for p in before})
        repeated = self.fx.invoke()
        self.assertNotEqual(0, repeated.returncode)

    def test_post_marker_error_preserves_backups_and_marks_partial(self):
        self.fx.seed_targets()
        result = self.fx.invoke(extra={"MONAD_ALPHA_M1_FAULT_POINT": "after-install-started"})
        self.assertNotEqual(0, result.returncode)
        self.assertTrue((self.fx.backup / "install-started").is_file())
        self.assertTrue((self.fx.backup / "install-partial").is_file())
        self.assertTrue((self.fx.backup / "backup-manifest.json").is_file())
        blocked = self.fx.invoke(transaction="txn-session34-b")
        self.assertNotEqual(0, blocked.returncode)
        self.assertIn("Incomplete transaction", blocked.stderr)

    def test_each_inter_rename_failure_leaves_recoverable_partial_state(self):
        for point, expected_published in (("after-runner-rename", 1),
                                         ("after-support-rename", 2),
                                         ("after-sudoers-rename", 3)):
            with self.subTest(point=point):
                self.tearDown()
                self.setUp()
                self.fx.seed_targets()
                result = self.fx.invoke(extra={"MONAD_ALPHA_M1_FAULT_POINT": point})
                self.assertNotEqual(0, result.returncode)
                self.assertTrue((self.fx.backup / "install-partial").is_file())
                self.assertTrue((self.fx.backup / "backup-manifest.json").is_file())
                candidate_files = (self.fx.runner_dir / "athena-alpha-m1-runner",
                                   self.fx.runner_dir / "gateway_support.py",
                                   self.fx.sudoers_dir / "athena-alpha-m1")
                src_files = (self.fx.stage / "athena-alpha-m1-runner.py",
                             self.fx.stage / "gateway_support.py",
                             self.fx.stage / "athena-alpha-m1-sudoers")
                self.assertEqual(expected_published,
                                 sum(dst.exists() and digest(dst) == digest(src)
                                     for dst, src in zip(candidate_files, src_files)))

    def test_sigkill_after_marker_leaves_started_without_false_success(self):
        result = self.fx.invoke(extra={"MONAD_ALPHA_M1_FAULT_POINT": "kill-after-install-started"})
        self.assertEqual(-signal.SIGKILL, result.returncode)
        self.assertTrue((self.fx.backup / "install-started").is_file())
        self.assertFalse((self.fx.backup / "install-complete").exists())
        self.assertFalse((self.fx.backup / "install-partial").exists())
        blocked = self.fx.invoke(transaction="txn-session34-b")
        self.assertNotEqual(0, blocked.returncode)
        self.assertIn("Incomplete transaction", blocked.stderr)

    def test_sigint_and_sigterm_after_marker_preserve_partial_state(self):
        for sig in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signal=sig.name):
                self.tearDown()
                self.setUp()
                process = subprocess.Popen(
                    ["bash", str(INSTALLER), "install", str(self.fx.backup)],
                    env=self.fx.env(MONAD_ALPHA_M1_TEST_PAUSE_AT="after-install-started"),
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                )
                marker = self.fx.backup / "install-started"
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline and not marker.exists():
                    if process.poll() is not None:
                        break
                    time.sleep(0.01)
                self.assertTrue(marker.is_file(), "installer did not reach post-marker pause")
                time.sleep(0.05)
                process.send_signal(sig)
                stdout, stderr = process.communicate(timeout=5)
                self.assertNotEqual(0, process.returncode, stdout)
                self.assertTrue((self.fx.backup / "install-partial").is_file(), stderr)
                self.assertFalse((self.fx.backup / "install-complete").exists())

    def test_rollback_denial_preserves_backup_receipt_and_targets(self):
        self.fx.seed_targets()
        installed = self.fx.invoke()
        self.assertEqual(0, installed.returncode, installed.stderr)
        before = self.fx.evidence_snapshot()
        approval = self.fx.approval(alter={"transaction_sha256": "0" * 64})
        result = self.fx.invoke("rollback", approval=approval)
        self.assertNotEqual(0, result.returncode)
        self.assertFalse((self.fx.backup / "rollback-started").exists())
        self.assertEqual(before, self.fx.evidence_snapshot())

    def test_rollback_requires_explicit_identity_and_bound_hashes(self):
        self.fx.seed_targets()
        installed = self.fx.invoke()
        self.assertEqual(0, installed.returncode, installed.stderr)
        bad_id = self.fx.invoke("rollback", transaction="another-id", approval=self.fx.approval())
        self.assertNotEqual(0, bad_id.returncode)
        self.assertFalse((self.fx.backup / "rollback-started").exists())
        approval = self.fx.approval()
        result = self.fx.invoke("rollback", approval=approval)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertTrue((self.fx.backup / "rollback-started").is_file())
        self.assertTrue((self.fx.backup / "rollback-complete").is_file())
        self.assertEqual(b"old runner\n", (self.fx.runner_dir / "athena-alpha-m1-runner").read_bytes())
        self.assertEqual(b"old support\n", (self.fx.runner_dir / "gateway_support.py").read_bytes())
        self.assertEqual(b"old sudoers\n", (self.fx.sudoers_dir / "athena-alpha-m1").read_bytes())
        repeated = self.fx.invoke("rollback", approval=approval)
        self.assertNotEqual(0, repeated.returncode)

    def test_partial_rollback_stops_after_one_restore_and_cannot_be_retried(self):
        self.fx.seed_targets()
        installed = self.fx.invoke()
        self.assertEqual(0, installed.returncode, installed.stderr)
        approval = self.fx.approval()
        result = self.fx.invoke(
            "rollback", approval=approval,
            extra={"MONAD_ALPHA_M1_FAULT_POINT": "after-rollback-runner-restore"},
        )
        self.assertNotEqual(0, result.returncode)
        self.assertTrue((self.fx.backup / "rollback-started").is_file())
        self.assertTrue((self.fx.backup / "rollback-partial").is_file())
        self.assertEqual(b"old runner\n", (self.fx.runner_dir / "athena-alpha-m1-runner").read_bytes())
        self.assertEqual(b"candidate support\n", (self.fx.runner_dir / "gateway_support.py").read_bytes())
        retry = self.fx.invoke("rollback", approval=approval)
        self.assertNotEqual(0, retry.returncode)
        self.assertEqual(b"candidate support\n", (self.fx.runner_dir / "gateway_support.py").read_bytes())

    def test_target_tampering_rejects_approved_rollback_without_evidence_mutation(self):
        self.fx.seed_targets()
        installed = self.fx.invoke()
        self.assertEqual(0, installed.returncode, installed.stderr)
        approval = self.fx.approval()
        (self.fx.runner_dir / "athena-alpha-m1-runner").write_bytes(b"unexpected generation\n")
        before = self.fx.evidence_snapshot()
        result = self.fx.invoke("rollback", approval=approval)
        self.assertNotEqual(0, result.returncode)
        self.assertFalse((self.fx.backup / "rollback-started").exists())
        self.assertEqual(before, self.fx.evidence_snapshot())

    def test_approval_symlink_and_hardlink_are_rejected_without_mutation(self):
        self.fx.seed_targets()
        installed = self.fx.invoke()
        self.assertEqual(0, installed.returncode, installed.stderr)
        approval = self.fx.approval()
        for path, create in (
            (self.fx.root / "approval-symlink.json", lambda p: p.symlink_to(approval)),
            (self.fx.root / "approval-hardlink.json", lambda p: os.link(approval, p)),
        ):
            with self.subTest(path=path.name):
                create(path)
                before = self.fx.evidence_snapshot()
                result = self.fx.invoke("rollback", approval=path)
                self.assertNotEqual(0, result.returncode)
                self.assertFalse((self.fx.backup / "rollback-started").exists())
                self.assertEqual(before, self.fx.evidence_snapshot())

    def test_rollback_rejects_missing_or_extra_transaction_record_arrays(self):
        for mutation in (
            lambda record: record["destinations"].pop(),
            lambda record: record["originals"].append({"present": False}),
            lambda record: record["candidates"].append("0" * 64),
        ):
            with self.subTest(mutation=mutation):
                self.tearDown()
                self.setUp()
                self.fx.seed_targets()
                installed = self.fx.invoke()
                self.assertEqual(0, installed.returncode, installed.stderr)
                self.fx.rewrite_transaction(mutation)
                approval = self.fx.approval()
                before = self.fx.evidence_snapshot()
                result = self.fx.invoke("rollback", approval=approval)
                self.assertNotEqual(0, result.returncode)
                self.assertFalse((self.fx.backup / "rollback-started").exists())
                self.assertEqual(before, self.fx.evidence_snapshot())

    def test_concurrent_invocation_is_rejected_by_persistent_lock(self):
        first = subprocess.Popen(["bash", str(INSTALLER), "install", str(self.fx.backup)],
                                 env=self.fx.env(MONAD_ALPHA_M1_TEST_HOLD_LOCK="1"),
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        lock = self.fx.state_root / "admin-install.lock"
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline and not lock.exists():
            time.sleep(0.01)
        self.assertTrue(lock.exists())
        second = self.fx.invoke(transaction="txn-session34-b")
        first.communicate(timeout=10)
        self.assertNotEqual(0, second.returncode)
        self.assertIn("Another Alpha M1 administrative transaction holds the lock", second.stderr)
        self.assertTrue(lock.is_file())


if __name__ == "__main__":
    unittest.main()
