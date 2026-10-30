"""Synthetic adversarial tests for the Session 58 trust-chain candidate."""
from __future__ import annotations

import hashlib
import hmac
import json
import os
from pathlib import Path
import shutil
import tempfile
import threading
import time
import unittest
import uuid
from unittest import mock

from qualification.alpha_m1_collector_v2.candidate_identity import (
    AUTHORIZED_COMPONENT_PATHS, aggregate_digest, validate_identity_manifest,
)
from qualification.alpha_m1_collector_v2.contracts import InvalidRequest
from qualification.alpha_m1_collector_v2.nonce_ledger import (
    LEDGER_NAME, LOCK_NAME, NonceLedger, initialize_synthetic_ledger,
)
from qualification.alpha_m1_collector_v2.target_attestation import (
    VerifiedTarget, verify_target_attestation,
)
from qualification.alpha_m1_collector_v2.trust_chain import (
    POLICY_VERSION, canonical_json,
)
from qualification.alpha_m1_collector_v2.verified_launcher import Approval, launch_synthetic

ROOT = Path(__file__).resolve().parents[1]
PACKAGE_ROOT = ROOT / "qualification/alpha_m1_collector_v2"


def sign(key: bytes, payload: dict) -> str:
    return hmac.new(key, canonical_json(payload), hashlib.sha256).hexdigest()


class TrustChainTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.candidate = self.root / "candidate"
        self.trust = self.root / "trust"
        self.ledger = self.root / "ledger"
        for path in (self.candidate, self.trust, self.ledger):
            path.mkdir(mode=0o700)
        self.keys = {
            "candidate": b"c" * 32, "read": b"r" * 32,
            "execution": b"e" * 32, "operator": b"o" * 32,
        }
        self.key_ids = {name: "test-" + name for name in self.keys}
        self.root_key = b"root-fixture-key-0123456789abcdef"
        roles = {
            "candidate-approver": [self._key_row("candidate")],
            "read-set-approver": [self._key_row("read")],
            "execution-approver": [self._key_row("execution")],
            "operator-target-attestor": [self._key_row("operator")],
        }
        unsigned = {"schema_version": 1, "policy_version": POLICY_VERSION, "roles": roles}
        trust_doc = {**unsigned, "root_signature": sign(self.root_key, unsigned)}
        (self.trust / "trust-store.json").write_bytes(canonical_json(trust_doc))
        os.chmod(self.trust / "trust-store.json", 0o600)
        (self.ledger / LOCK_NAME).touch(mode=0o600)
        ledger_fd = self._dirfd(self.ledger)
        try:
            initialize_synthetic_ledger(ledger_fd, root_anchor=self.root_key)
        finally:
            os.close(ledger_fd)
        self.manifest_raw = self._populate_candidate()
        self.manifest = json.loads(self.manifest_raw)
        self.target_id = "athena-fixture"
        self.now = int(time.time())
        ident = {
            "target_id": self.target_id, "machine_id": "a" * 32,
            "hostname": "fixture-host", "boot_id": str(uuid.uuid4()),
            "snapshot_id": "snapshot-fixture",
        }
        self.attestation = self._target(ident, ident)
        digest = hashlib.sha256(self.manifest_raw).hexdigest()
        self.candidate_approval = Approval(
            "candidate-identity", self.key_ids["candidate"], "candidate-human", self.now - 1,
            self.now + 100, "candidate-nonce-1234567890", digest, self.target_id, (), {}, "",
        )
        self.candidate_approval = self._signed_approval(self.candidate_approval, "candidate")
        modules = tuple(self.manifest["read_set_maximum"])
        budgets = self.manifest["budget_maximum"]
        nonce = "nonce-0123456789abcdefghijkl"
        self.read_approval = self._signed_approval(Approval(
            "read-set", self.key_ids["read"], "read-human", self.now - 1,
            self.now + 100, nonce, digest, self.target_id, modules, budgets, "",
        ), "read")
        self.execution_approval = self._signed_approval(Approval(
            "execution", self.key_ids["execution"], "execution-human", self.now - 1,
            self.now + 100, nonce, digest, self.target_id, modules, budgets, "",
        ), "execution")

    def tearDown(self):
        self.temp.cleanup()

    def _key_row(self, name):
        principals = {"candidate": "candidate-human", "read": "read-human",
                      "execution": "execution-human", "operator": "operator-fixture"}
        return {"key_id": self.key_ids[name], "key_hex": self.keys[name].hex(),
                "principal_id": principals[name]}

    @staticmethod
    def _dirfd(path):
        return os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)

    def _populate_candidate(self):
        rows = []
        for relative in AUTHORIZED_COMPONENT_PATHS:
            src = ROOT / relative
            dst = self.candidate / relative
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, dst)
            os.chmod(dst, 0o600)
            raw = dst.read_bytes()
            rows.append({"path": relative, "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()})
        from qualification.alpha_m1_collector_v2.candidate_identity import (
            AUTHORIZATION_CONTRACT_VERSION, ENTRYPOINT_CALLABLE, ENTRYPOINT_PATH,
            READ_SET_HARD_MAXIMUM,
        )
        from qualification.alpha_m1_collector_v2.contracts import BudgetLimits
        budget = BudgetLimits().__dict__
        manifest = {
            "schema_version": 1, "candidate_id": "alpha-m1-collector-v2-core",
            "authorization_contract_version": AUTHORIZATION_CONTRACT_VERSION,
            "components": rows, "aggregate_sha256": aggregate_digest(rows),
            "entrypoint": {"path": ENTRYPOINT_PATH, "callable": ENTRYPOINT_CALLABLE},
            "read_set_maximum": list(READ_SET_HARD_MAXIMUM), "budget_maximum": budget,
        }
        return canonical_json(manifest) + b"\n"

    def _target(self, declared, observed):
        item = VerifiedTarget(declared, observed, "synthetic-provider-v1", "fixture-evidence",
                              "operator-fixture", self.now - 1, self.now + 100,
                              observed["snapshot_id"], self.key_ids["operator"], "")
        return VerifiedTarget(**{**item.__dict__, "signature": sign(self.keys["operator"], item.unsigned_payload())})

    def _signed_approval(self, approval, key_name):
        return Approval(**{**approval.__dict__, "signature": sign(self.keys[key_name], approval.payload())})

    def _launch(self, **overrides):
        args = {
            "candidate_root_fd": self._dirfd(self.candidate),
            "trust_directory_fd": self._dirfd(self.trust),
            "ledger_directory_fd": self._dirfd(self.ledger),
            "root_anchor": self.root_key, "manifest_raw": self.manifest_raw,
            "candidate_approval": self.candidate_approval,
            "target_attestation": self.attestation,
            "readset_approval": self.read_approval,
            "execution_approval": self.execution_approval,
            "expected_target_id": self.target_id,
        }
        args.update(overrides)
        fds = [args[k] for k in ("candidate_root_fd", "trust_directory_fd", "ledger_directory_fd")]
        try:
            return launch_synthetic(**args)
        finally:
            for fd in fds:
                os.close(fd)

    def test_valid_chain_runs_only_synthetic_and_consumes_nonce(self):
        result = self._launch()
        self.assertEqual(result["completion"], "SYNTHETIC_ONLY")
        self.assertFalse(result["entry_result"]["collector_started"])
        with self.assertRaises(InvalidRequest):
            self._launch()

    def test_checked_in_identity_manifest_matches_all_component_bytes(self):
        raw = (PACKAGE_ROOT / "candidate_identity.json").read_bytes()
        identity = validate_identity_manifest(json.loads(raw))
        for record in identity["components"]:
            observed = (ROOT / record["path"]).read_bytes()
            self.assertEqual(len(observed), record["bytes"], record["path"])
            self.assertEqual(hashlib.sha256(observed).hexdigest(), record["sha256"], record["path"])

    def test_tampered_candidate_component_rejected_before_nonce(self):
        path = self.candidate / AUTHORIZED_COMPONENT_PATHS[0]
        path.write_bytes(path.read_bytes() + b"# mutation\n")
        with self.assertRaises(InvalidRequest):
            self._launch()

    def test_operational_mode_fails_closed(self):
        with self.assertRaises(InvalidRequest):
            self._launch(operational=True)

    def test_target_mismatch_expiry_and_missing_evidence_rejected(self):
        with self.assertRaises(InvalidRequest):
            self._launch(expected_target_id="other-target")
        expired = VerifiedTarget(**{**self.attestation.__dict__, "expires_at": self.now})
        with self.assertRaises(InvalidRequest):
            self._launch(target_attestation=expired)
        mismatch = dict(self.attestation.observed_identity)
        mismatch["hostname"] = "different"
        with self.assertRaises(InvalidRequest):
            self._launch(target_attestation=self._target(self.attestation.declared_identity, mismatch))

    def test_scope_budget_role_and_trust_store_changes_rejected(self):
        wrong = self._signed_approval(Approval(**{
            **self.read_approval.__dict__, "modules": ("identity",), "signature": "",
        }), "read")
        with self.assertRaises(InvalidRequest):
            self._launch(readset_approval=wrong)
        same_person = self._signed_approval(Approval(**{
            **self.execution_approval.__dict__, "approver_id": "read-human", "signature": "",
        }), "execution")
        with self.assertRaises(InvalidRequest):
            self._launch(execution_approval=same_person)
        float_budget = dict(self.read_approval.budgets)
        first_key = next(iter(float_budget))
        float_budget[first_key] = float(float_budget[first_key])
        malformed_budget = self._signed_approval(Approval(**{
            **self.read_approval.__dict__, "budgets": float_budget, "signature": "",
        }), "read")
        with self.assertRaises(InvalidRequest):
            self._launch(readset_approval=malformed_budget)
        (self.trust / "trust-store.json").write_bytes(b"{}")
        with self.assertRaises(InvalidRequest):
            self._launch()

    def test_manifest_digest_and_entrypoint_change_rejected(self):
        changed = self.manifest_raw.replace(b"synthetic_entry.py", b"other_entrypoint.py")
        with self.assertRaises(Exception):
            self._launch(manifest_raw=changed)

    def test_symlink_and_hardlink_candidates_rejected(self):
        path = self.candidate / AUTHORIZED_COMPONENT_PATHS[0]
        backup = path.with_suffix(".saved")
        path.rename(backup)
        path.symlink_to(backup.name)
        with self.assertRaises(OSError):
            self._launch()
        path.unlink()
        os.link(backup, path)
        with self.assertRaises(InvalidRequest):
            self._launch()

    def test_corrupt_ledger_fails_closed(self):
        (self.ledger / LEDGER_NAME).write_bytes(b"not json")
        with self.assertRaises(InvalidRequest):
            self._launch()

    def test_staging_write_or_sync_failure_leaves_fail_closed_state(self):
        import qualification.alpha_m1_collector_v2.nonce_ledger as ledger_module
        fd = self._dirfd(self.ledger)
        try:
            with NonceLedger(fd, root_anchor=self.root_key) as ledger:
                with mock.patch.object(ledger_module, "_write_all", side_effect=OSError("ENOSPC fixture")):
                    with self.assertRaises(OSError):
                        ledger.consume("nonce-0123456789abcdefghijkl", "fixture-crash")
        finally:
            os.close(fd)
        self.assertTrue((self.ledger / ".nonce-ledger.next").exists())
        with self.assertRaises(InvalidRequest):
            self._launch()

    def test_rename_failure_does_not_run_entry_and_stale_stage_blocks_retry(self):
        import qualification.alpha_m1_collector_v2.nonce_ledger as ledger_module
        fd = self._dirfd(self.ledger)
        try:
            with NonceLedger(fd, root_anchor=self.root_key) as ledger:
                with mock.patch.object(ledger_module.os, "rename", side_effect=OSError("rename fixture")):
                    with self.assertRaises(OSError):
                        ledger.consume("nonce-0123456789abcdefghijkl", "fixture-rename")
        finally:
            os.close(fd)
        with self.assertRaises(InvalidRequest):
            self._launch()
        self.assertFalse((self.ledger / LEDGER_NAME).read_bytes() == b"")

    def test_missing_trust_store_fails_closed(self):
        (self.trust / "trust-store.json").unlink()
        with self.assertRaises(OSError):
            self._launch()

    def test_ledger_setup_failures_close_descriptors(self):
        import qualification.alpha_m1_collector_v2.nonce_ledger as ledger_module
        original_close = os.close
        closed = []

        def tracking_close(fd):
            closed.append(fd)
            return original_close(fd)

        os.chmod(self.ledger / LOCK_NAME, 0o644)
        directory_fd = self._dirfd(self.ledger)
        with mock.patch.object(ledger_module.os, "close", side_effect=tracking_close):
            with self.assertRaises(InvalidRequest):
                NonceLedger(directory_fd, root_anchor=self.root_key).__enter__()
        self.assertGreaterEqual(len(closed), 2)  # lock descriptor and duplicated directory descriptor
        original_close(directory_fd)

        os.chmod(self.ledger / LOCK_NAME, 0o600)
        blocker = os.open(self.ledger / LOCK_NAME, os.O_RDWR)
        import fcntl
        fcntl.flock(blocker, fcntl.LOCK_EX | fcntl.LOCK_NB)
        directory_fd = self._dirfd(self.ledger)
        closed.clear()
        try:
            with mock.patch.object(ledger_module.os, "close", side_effect=tracking_close):
                with self.assertRaises(InvalidRequest):
                    NonceLedger(directory_fd, root_anchor=self.root_key,
                                timeout_seconds=0.02).__enter__()
            self.assertGreaterEqual(len(closed), 2)
        finally:
            original_close(directory_fd)
            fcntl.flock(blocker, fcntl.LOCK_UN)
            original_close(blocker)

    def test_ledger_exit_closes_directory_if_unlock_fails(self):
        import fcntl
        import qualification.alpha_m1_collector_v2.nonce_ledger as ledger_module
        fd = self._dirfd(self.ledger)
        ledger = NonceLedger(fd, root_anchor=self.root_key)
        os.close(fd)
        ledger.__enter__()
        duplicated_dir_fd = ledger._dir_fd
        lock_fd = ledger._lock_fd
        real_flock = fcntl.flock

        def fail_unlock(descriptor, operation):
            if operation == fcntl.LOCK_UN:
                raise OSError("synthetic unlock failure")
            return real_flock(descriptor, operation)

        with mock.patch.object(ledger_module.fcntl, "flock", side_effect=fail_unlock):
            with self.assertRaises(OSError):
                ledger.__exit__(None, None, None)
        self.assertEqual(ledger._dir_fd, -1)
        for descriptor in (duplicated_dir_fd, lock_fd):
            with self.assertRaises(OSError):
                os.fstat(descriptor)

    def test_ledger_consume_survives_restart_and_restored_snapshot_can_replay(self):
        before = (self.ledger / LEDGER_NAME).read_bytes()
        fd = self._dirfd(self.ledger)
        try:
            with NonceLedger(fd, root_anchor=self.root_key) as ledger:
                ledger.consume("nonce-0123456789abcdefghijkl", "fixture-attempt")
        finally:
            os.close(fd)
        with self.assertRaises(InvalidRequest):
            self._launch()
        # Demonstrate the documented local-storage rollback limit. A same-UID
        # actor restoring a previous valid image also restores its valid MAC.
        (self.ledger / LEDGER_NAME).write_bytes(before)
        result = self._launch()
        self.assertEqual(result["completion"], "SYNTHETIC_ONLY")

    def test_two_concurrent_launches_consume_nonce_at_most_once(self):
        barrier = threading.Barrier(2)
        outcomes = []

        def attempt():
            barrier.wait()
            try:
                outcomes.append(("ok", self._launch()["completion"]))
            except Exception as exc:
                outcomes.append(("error", type(exc).__name__))

        threads = [threading.Thread(target=attempt) for _ in range(2)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(timeout=5)
        self.assertTrue(all(not thread.is_alive() for thread in threads))
        self.assertEqual(sum(kind == "ok" for kind, _ in outcomes), 1)
        self.assertEqual(sum(kind == "error" for kind, _ in outcomes), 1)

    def test_expired_and_forged_approvals_fail_before_nonce(self):
        expired = Approval(**{**self.read_approval.__dict__, "expires_at": self.now,
                              "signature": ""})
        expired = self._signed_approval(expired, "read")
        with self.assertRaises(InvalidRequest):
            self._launch(readset_approval=expired)
        forged = Approval(**{**self.read_approval.__dict__, "target_id": "other",
                             "signature": self.read_approval.signature})
        with self.assertRaises(InvalidRequest):
            self._launch(readset_approval=forged)
        forged_principal = self._signed_approval(Approval(**{
            **self.read_approval.__dict__, "approver_id": "impersonated-human", "signature": "",
        }), "read")
        with self.assertRaises(InvalidRequest):
            self._launch(readset_approval=forged_principal)
        # Original authorization remains usable because rejected attempts do
        # not consume the nonce.
        self.assertEqual(self._launch()["completion"], "SYNTHETIC_ONLY")


if __name__ == "__main__":
    unittest.main()
