"""Synthetic security tests for the isolated Alpha M1 v2 collector core."""
from __future__ import annotations

import hashlib
import hmac
import json
import os
from pathlib import Path
import tempfile
import threading
import unittest
import uuid
from unittest import mock

from qualification.alpha_m1_collector_v2.budget import Budget
from qualification.alpha_m1_collector_v2.contracts import (
    BudgetExhausted, BudgetLimits, ConcurrentUse, InvalidRequest, ParseFailure, ResultStatus, UnsafePath,
)
from qualification.alpha_m1_collector_v2.adapters import (
    CgroupMembership, EnumerationPage, ProcessReference, alpha_unit_id,
)
from qualification.alpha_m1_collector_v2.authorization import (
    AuthorizationBundle, AuthorizationVerifier, ExecutionApproval, ReadSetApproval,
)
from qualification.alpha_m1_collector_v2.orchestrator import CollectorOrchestrator, verify_finalization
from qualification.alpha_m1_collector_v2.orchestrator import _aggregate_status, _status_for_exception
from qualification.alpha_m1_collector_v2.results import ModuleResult
from qualification.alpha_m1_collector_v2.safe_fs import (
    PartialWriteError, SafeDirectory, open_absolute_directory,
)


def limits(**values: int) -> BudgetLimits:
    base = BudgetLimits(bytes_read=2 * 1024 * 1024, bytes_persisted=256 * 1024, files=32,
                        records=32, processes=8, units=8, duration_seconds=30,
                        bytes_per_file=1024 * 1024, bytes_per_source=2 * 1024 * 1024,
                        output_file_bytes=64 * 1024)
    return BudgetLimits(**{**base.__dict__, **values})


TEST_KEY_ID = "synthetic-key"
TEST_KEY = b"S" * 32
TEST_EXEC_KEY_ID = "synthetic-execution-key"
TEST_EXEC_KEY = b"E" * 32
TEST_TARGET = "fixture-vm-01"
TEST_CANDIDATE_DIGEST = hashlib.sha256(b"synthetic-candidate-v2").hexdigest()
TEST_NOW = 2_000_000_000


def _sign(payload: dict[str, object], key: bytes = TEST_KEY) -> str:
    raw = json.dumps(payload, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("ascii")
    return hmac.new(key, raw, hashlib.sha256).hexdigest()


def approval_bundle(modules: tuple[str, ...], *, authorized: tuple[str, ...] | None = None,
                    auth_limits: BudgetLimits | None = None, issued: int = TEST_NOW - 1,
                    expires: int = TEST_NOW + 300, read_decision: str = "APPROVE",
                    exec_decision: str = "APPROVE", target: str = TEST_TARGET,
                    budget_payload: dict[str, int] | None = None,
                    same_approver: bool = False, same_key: bool = False,
                    same_secret: bool = False) -> AuthorizationBundle:
    authorized = modules if authorized is None else authorized
    auth_limits = limits() if auth_limits is None else auth_limits
    read = ReadSetApproval(
        approval_id=str(uuid.uuid4()), candidate_id="alpha-m1-collector-v2-core",
        candidate_digest=TEST_CANDIDATE_DIGEST, requested_read_set=modules,
        authorized_modules=authorized,
        authorized_budgets=auth_limits.__dict__ if budget_payload is None else budget_payload,
        target_id=target, decision=read_decision, decision_maker="synthetic-reviewer",
        issued_at=issued, expires_at=expires, nonce=str(uuid.uuid4()),
        key_id=TEST_KEY_ID, signature="0" * 64,
    )
    read = ReadSetApproval(**{**read.__dict__, "signature": _sign(read.unsigned_payload())})
    execution = ExecutionApproval(
        approval_id=str(uuid.uuid4()), read_set_approval_id=read.approval_id,
        candidate_id=read.candidate_id, candidate_digest=read.candidate_digest,
        target_id=target, decision=exec_decision,
        decision_maker="synthetic-reviewer" if same_approver else "synthetic-operator",
        issued_at=issued, expires_at=expires, nonce=str(uuid.uuid4()),
        key_id=TEST_KEY_ID if same_key else TEST_EXEC_KEY_ID, signature="0" * 64,
    )
    execution_key = TEST_KEY if same_key or same_secret else TEST_EXEC_KEY
    execution = ExecutionApproval(**{**execution.__dict__, "signature": _sign(execution.unsigned_payload(), execution_key)})
    return AuthorizationBundle(read, execution)


def orchestrator(limits_value: BudgetLimits | None = None,
                 verifier: AuthorizationVerifier | None = None,
                 target: str = TEST_TARGET) -> CollectorOrchestrator:
    return CollectorOrchestrator(
        limits=limits() if limits_value is None else limits_value,
        authorization_verifier=AuthorizationVerifier(
            {TEST_KEY_ID: TEST_KEY, TEST_EXEC_KEY_ID: TEST_EXEC_KEY}, clock=lambda: TEST_NOW)
        if verifier is None else verifier,
        candidate_digest=TEST_CANDIDATE_DIGEST,
        target_id=target,
    )


VALID_IDENTITY = {
    "system_id": "0123456789abcdef0123456789abcdef",
    "kernel": "6.12.1-amd64",
    "distribution": "Debian-13.7",
    "virtualization": "oracle",
    "boot_id": "5b3ea61e-db80-4954-af5f-825dbda5fc6e",
    "acquisition_environment_id": "fixture-guest-01",
}


def fixture_size(value) -> int:
    if hasattr(value, "__dataclass_fields__"):
        value = {name: getattr(value, name) for name in value.__dataclass_fields__}
    return len(json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("ascii"))


class FixtureSource:
    """In-memory test source. It has no filesystem or system-service backend."""

    def __init__(self, *, identity: dict[str, object] | None = None,
                 units: list[dict[str, object]] | None = None,
                 process_refs: list[ProcessReference] | None = None,
                 process_snapshots: dict[int, dict[str, object] | None] | None = None,
                 cgroups: dict[int, str | None] | None = None,
                 unit_page_interrupt: bool = False, pid_page_interrupt: bool = False,
                 unit_page_exception: type[BaseException] | None = None,
                 pid_page_exception: type[BaseException] | None = None):
        self.identity = VALID_IDENTITY if identity is None else identity
        self.units = [] if units is None else units
        self.process_refs = [] if process_refs is None else process_refs
        self.process_snapshots = {} if process_snapshots is None else process_snapshots
        self.cgroups = {} if cgroups is None else cgroups
        self.unit_page_interrupt = unit_page_interrupt
        self.pid_page_interrupt = pid_page_interrupt
        self.unit_page_exception = unit_page_exception
        self.pid_page_exception = pid_page_exception
        self.calls: list[str] = []

    def read_identity(self, budget):
        self.calls.append("identity")
        budget.record_source_bytes(fixture_size(self.identity))
        return self.identity

    def enumerate_units(self, maximum, budget):
        self.calls.append("unit-list")
        source = self
        def values():
            for index, item in enumerate(source.units[:maximum]):
                budget.record_source_bytes(fixture_size(item["unit_id"]))
                yield item["unit_id"]
                if source.unit_page_exception and index == 0:
                    raise source.unit_page_exception()
        return EnumerationPage(values(), len(self.units) <= maximum and not self.unit_page_interrupt)

    def read_unit(self, unit_id, budget):
        self.calls.append("unit:" + unit_id)
        value = next((row for row in self.units if row["unit_id"] == unit_id), None)
        budget.record_source_bytes(fixture_size(value))
        return value

    def enumerate_processes(self, maximum, budget):
        self.calls.append("pid-list")
        source = self
        snapshot_id = "fixture-epoch-01"
        budget.record_source_bytes(fixture_size(snapshot_id))
        def values():
            for index, item in enumerate(source.process_refs[:maximum]):
                budget.record_source_bytes(fixture_size(item))
                yield item
                if source.pid_page_exception and index == 0:
                    raise source.pid_page_exception()
        return EnumerationPage(values(), len(self.process_refs) <= maximum and not self.pid_page_interrupt,
                               snapshot_id=snapshot_id)

    def read_process(self, reference, budget):
        self.calls.append(f"process:{reference.pid}")
        value = self.process_snapshots.get(reference.pid)
        budget.record_source_bytes(fixture_size(value))
        return value

    def read_cgroup(self, reference, budget):
        self.calls.append(f"cgroup:{reference.pid}")
        value = self.cgroups.get(reference.pid)
        membership = CgroupMembership(reference.pid, reference.source_id,
                                      reference.snapshot_id, value)
        budget.record_source_bytes(fixture_size(membership))
        return membership


class SafeFilesystemTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.fd = open_absolute_directory(str(self.root))
        self.budget = Budget(limits())

    def tearDown(self) -> None:
        self.fd.close()
        self.temp.cleanup()

    def test_open_absolute_path_rejects_intermediate_and_final_symlinks(self) -> None:
        real = self.root / "real"
        real.mkdir()
        (self.root / "link").symlink_to(real, target_is_directory=True)
        (self.root / "final-link").symlink_to(real, target_is_directory=True)
        with self.assertRaises((OSError, UnsafePath)):
            open_absolute_directory(str(self.root / "link"))
        with self.assertRaises((OSError, UnsafePath)):
            open_absolute_directory(str(self.root / "final-link"))
        with self.assertRaises((OSError, UnsafePath)):
            open_absolute_directory(str(self.root / "real" / "missing" / ".."))

    def test_leaf_symlink_hard_link_fifo_and_special_are_rejected(self) -> None:
        source = self.root / "source"
        source.write_bytes(b"fixture")
        (self.root / "sym").symlink_to(source)
        os.link(source, self.root / "hard")
        os.mkfifo(self.root / "pipe")
        for name in ("sym", "hard", "pipe"):
            with self.subTest(name=name):
                with self.assertRaises((OSError, UnsafePath)):
                    self.fd.open_regular(name, source="identity", budget=self.budget)
        with self.assertRaises(TypeError):
            self.fd.open_regular("source", source="identity", budget=self.budget,
                                 require_single_link=False)  # type: ignore[call-arg]

    def test_bounded_read_and_hash_and_accounting(self) -> None:
        (self.root / "data").write_bytes(b"abc123")
        with self.fd.open_regular("data", source="identity", budget=self.budget) as item:
            result = item.read_all()
        self.assertEqual(result.data, b"abc123")
        self.assertEqual(result.sha256, hashlib.sha256(b"abc123").hexdigest())
        self.assertEqual(self.budget.bytes_read, 6)
        with self.fd.open_regular("data", source="identity", budget=self.budget) as item:
            digest = item.hash_all()
        self.assertEqual(digest.data, b"")
        self.assertEqual(self.budget.bytes_read, 12)

    def test_oversized_file_and_global_and_source_budgets_are_explicit(self) -> None:
        (self.root / "big").write_bytes(b"x" * 20)
        with self.assertRaises(BudgetExhausted):
            self.fd.open_regular("big", source="identity", budget=Budget(limits(bytes_per_file=10)))
        global_budget = Budget(limits(bytes_read=4, bytes_per_file=10, bytes_per_source=10))
        with self.assertRaises(BudgetExhausted):
            global_budget.consume_read(5, "identity")
        source_budget = Budget(limits(bytes_read=20, bytes_per_file=20, bytes_per_source=4))
        with self.assertRaises(BudgetExhausted):
            source_budget.consume_read(5, "identity")
        with self.assertRaises(InvalidRequest):
            source_budget.consume_read(1, "attacker-selected-source")

    def test_read_allowance_never_reads_past_global_cap_and_charges_before_timeout(self) -> None:
        (self.root / "capped").write_bytes(b"0123456789")
        capped_budget = Budget(limits(bytes_read=4, bytes_per_source=10, bytes_per_file=10))
        physical = 0
        real_read = os.read
        def count_read(fd: int, amount: int) -> bytes:
            nonlocal physical
            result = real_read(fd, amount)
            physical += len(result)
            return result
        with self.fd.open_regular("capped", source="identity", budget=capped_budget) as item:
            with mock.patch("qualification.alpha_m1_collector_v2.safe_fs.os.read", side_effect=count_read):
                with self.assertRaises(BudgetExhausted):
                    item.read_all()
        self.assertEqual(physical, 4)
        self.assertEqual(capped_budget.bytes_read, 4)

        now = [0]
        timed_budget = Budget(limits(duration_seconds=0), clock=lambda: now[0])
        with self.fd.open_regular("capped", source="identity", budget=timed_budget,
                                  max_bytes=10) as item:
            def read_then_expire(fd: int, amount: int) -> bytes:
                data = real_read(fd, amount)
                now[0] = 1
                return data
            with mock.patch("qualification.alpha_m1_collector_v2.safe_fs.os.read", side_effect=read_then_expire):
                from qualification.alpha_m1_collector_v2.contracts import DeadlineExceeded
                with self.assertRaises(DeadlineExceeded):
                    item.read_all()
        self.assertEqual(timed_budget.bytes_read, 10)

    def test_process_unit_record_and_deadline_budgets(self) -> None:
        budget = Budget(limits(records=3, processes=1, units=1))
        budget.consume_record("process")
        with self.assertRaises(BudgetExhausted):
            budget.consume_record("process")
        budget.consume_record("unit")
        with self.assertRaises(BudgetExhausted):
            budget.consume_record("unit")
        with self.assertRaises(ValueError):
            budget.consume_record("unbounded-category")

    def test_finalization_reserve_is_not_available_to_adapter_output(self) -> None:
        budget = Budget(limits())
        adapter = __import__("qualification.alpha_m1_collector_v2.budget",
                              fromlist=["AdapterBudget"]).AdapterBudget(budget, "identity")
        self.assertFalse(hasattr(adapter, "record_finalization_persisted"))
        adapter_ceiling = budget.limits.bytes_persisted - budget.finalization_reserve
        remaining = adapter_ceiling
        while remaining:
            part = min(remaining, budget.limits.output_file_bytes)
            budget.record_persisted(part)
            remaining -= part
        with self.assertRaises(BudgetExhausted):
            budget.record_persisted(1)
        budget.record_finalization_persisted(budget.limits.manifest_bytes, file_kind="manifest")
        budget.record_finalization_persisted(budget.limits.seal_bytes, file_kind="seal")
        self.assertEqual(budget.bytes_persisted, budget.limits.bytes_persisted)
        self.assertEqual(budget.finalization_bytes, budget.finalization_reserve)

    def test_filesystem_writer_stops_before_consuming_finalization_reserve(self) -> None:
        out = self.fd.create_child_exclusive("reserve-output")
        budget = Budget(limits())
        ceiling = budget.limits.bytes_persisted - budget.finalization_reserve
        remaining = ceiling
        while remaining:
            amount = min(remaining, budget.limits.output_file_bytes)
            budget.record_persisted(amount)
            remaining -= amount
        attempted_writes: list[int] = []
        with mock.patch("qualification.alpha_m1_collector_v2.safe_fs.os.write",
                        side_effect=lambda fd, data: attempted_writes.append(len(data))):
            with self.assertRaises(PartialWriteError):
                out.write_new_file("blocked", b"x", budget=budget)
        self.assertEqual(attempted_writes, [])
        self.assertEqual(budget.bytes_persisted, ceiling)
        self.assertEqual((self.root / "reserve-output" / "blocked").read_bytes(), b"")
        out.close()

    def test_budget_rejects_concurrent_thread_use(self) -> None:
        budget = Budget(limits())
        budget.check_deadline()
        outcomes: list[BaseException | None] = []
        def use_from_other_thread():
            try:
                budget.check_deadline()
            except BaseException as exc:
                outcomes.append(exc)
        thread = threading.Thread(target=use_from_other_thread)
        thread.start()
        thread.join(timeout=2)
        self.assertFalse(thread.is_alive())
        self.assertEqual(len(outcomes), 1)
        self.assertIsInstance(outcomes[0], ConcurrentUse)

    def test_access_terminal_counts_balance_attempts(self) -> None:
        (self.root / "balanced").write_bytes(b"abc")
        with self.fd.open_regular("balanced", source="identity", budget=self.budget) as item:
            item.read_all()
        snapshot = self.budget.snapshot()
        for counts in snapshot["access_by_kind"].values():
            terminals = sum(counts.get(key, 0) for key in ("succeeded", "denied", "errors", "interrupted"))
            self.assertEqual(counts["attempted"], terminals)

    def test_changed_file_during_read_is_rejected(self) -> None:
        (self.root / "mutable").write_bytes(b"original")
        with self.fd.open_regular("mutable", source="identity", budget=self.budget) as item:
            real_read = os.read
            changed = False
            def mutate_after_first_read(fd: int, amount: int) -> bytes:
                nonlocal changed
                data = real_read(fd, amount)
                if not changed:
                    changed = True
                    with (self.root / "mutable").open("r+b") as target:
                        target.write(b"modified!")
                        target.flush()
                        os.fsync(target.fileno())
                return data
            with mock.patch("qualification.alpha_m1_collector_v2.safe_fs.os.read", side_effect=mutate_after_first_read):
                with self.assertRaises(UnsafePath):
                    item.read_all()

    def test_renamed_directory_does_not_redirect_open_descriptor(self) -> None:
        sub = self.root / "sub"
        sub.mkdir()
        (sub / "one").write_bytes(b"one")
        opened = self.fd.child_directory("sub")
        sub.rename(self.root / "old-sub")
        sub.mkdir()
        (sub / "one").write_bytes(b"two")
        try:
            with opened.open_regular("one", source="identity", budget=self.budget) as item:
                self.assertEqual(item.read_all().data, b"one")
        finally:
            opened.close()

    def test_exclusive_output_collision_and_partial_write(self) -> None:
        out = self.fd.create_child_exclusive("out")
        budget = Budget(limits())
        first = out.write_new_file("one", b"first", budget=budget)
        self.assertEqual(first.sha256, hashlib.sha256(b"first").hexdigest())
        with self.assertRaises(FileExistsError):
            out.write_new_file("one", b"replacement", budget=budget)

        real_write = os.write
        calls = 0
        def fail_after_progress(fd: int, data: bytes | memoryview) -> int:
            nonlocal calls
            calls += 1
            if calls == 1:
                return real_write(fd, data[:3])
            raise OSError("synthetic write failure")
        try:
            with mock.patch("qualification.alpha_m1_collector_v2.safe_fs.os.write", side_effect=fail_after_progress):
                with self.assertRaises(PartialWriteError) as caught:
                    out.write_new_file("partial", b"abcdefgh", budget=budget)
        finally:
            out.close()
        self.assertEqual(caught.exception.bytes_written, 3)
        self.assertEqual((self.root / "out" / "partial").read_bytes(), b"abc")
        self.assertEqual(caught.exception.sha256, hashlib.sha256(b"abc").hexdigest())
        self.assertEqual(budget.bytes_persisted, 8)

    def test_short_writes_finish_and_enospc_partial_is_accounted(self) -> None:
        out = self.fd.create_child_exclusive("write-faults")
        budget = Budget(limits())
        real_write = os.write
        def short_write(fd: int, data: bytes | memoryview) -> int:
            return real_write(fd, data[:2])
        with mock.patch("qualification.alpha_m1_collector_v2.safe_fs.os.write", side_effect=short_write):
            result = out.write_new_file("short", b"abcdefgh", budget=budget)
        self.assertEqual(result.bytes_written, 8)
        self.assertEqual((self.root / "write-faults" / "short").read_bytes(), b"abcdefgh")
        calls = [0]
        def enospc_after_progress(fd: int, data: bytes | memoryview) -> int:
            calls[0] += 1
            if calls[0] == 1:
                return real_write(fd, data[:3])
            raise OSError(28, "synthetic ENOSPC")
        with mock.patch("qualification.alpha_m1_collector_v2.safe_fs.os.write", side_effect=enospc_after_progress):
            with self.assertRaises(PartialWriteError) as caught:
                out.write_new_file("enospc", b"abcdefgh", budget=budget)
        self.assertEqual(caught.exception.bytes_written, 3)
        self.assertEqual(caught.exception.cause.errno, 28)
        self.assertEqual((self.root / "write-faults" / "enospc").read_bytes(), b"abc")
        out.close()

    def test_successful_write_crossing_deadline_is_fully_attributed(self) -> None:
        out = self.fd.create_child_exclusive("timeout-output")
        now = [0]
        budget = Budget(limits(duration_seconds=0), clock=lambda: now[0])
        real_write = os.write
        def write_then_expire(fd: int, data: bytes | memoryview) -> int:
            count = real_write(fd, data)
            now[0] = 1
            return count
        try:
            with mock.patch("qualification.alpha_m1_collector_v2.safe_fs.os.write",
                            side_effect=write_then_expire):
                with self.assertRaises(PartialWriteError) as caught:
                    out.write_new_file("timed", b"written-before-timeout", budget=budget)
        finally:
            out.close()
        saved = (self.root / "timeout-output" / "timed").read_bytes()
        self.assertEqual(caught.exception.bytes_written, len(saved))
        self.assertEqual(budget.bytes_persisted, len(saved))
        self.assertEqual(hashlib.sha256(saved).hexdigest(),
                         hashlib.sha256(b"written-before-timeout").hexdigest())


class OrchestratorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.parent = open_absolute_directory(str(self.root))

    def tearDown(self) -> None:
        self.parent.close()
        self.temp.cleanup()

    def run_candidate(self, modules: tuple[str, ...], source: FixtureSource, *, auth=None,
                      budget_limits: BudgetLimits | None = None, verifier=None,
                      acquisition_id: str = "synthetic-run", interrupt_event=None):
        budget_limits = limits() if budget_limits is None else budget_limits
        verifier = AuthorizationVerifier({TEST_KEY_ID: TEST_KEY, TEST_EXEC_KEY_ID: TEST_EXEC_KEY},
                                        clock=lambda: TEST_NOW) if verifier is None else verifier
        bundle = approval_bundle(modules, auth_limits=budget_limits) if auth is None else auth
        candidate = orchestrator(budget_limits, verifier)
        return candidate.run(self.parent, authorization=bundle, requested_modules=modules,
                             source=source, acquisition_id=acquisition_id,
                             interrupt_event=interrupt_event)

    def test_signed_read_and_separate_execution_approvals_fail_closed(self) -> None:
        source = FixtureSource()
        with self.assertRaises(InvalidRequest):
            orchestrator().run(self.parent, authorization=None, requested_modules=("identity",), source=source)
        read_only = approval_bundle(("identity",))
        with self.assertRaises(InvalidRequest):
            self.run_candidate(("identity",), source,
                               auth=AuthorizationBundle(read_only.read_set, None))
        for altered in (
            approval_bundle(("identity",), expires=TEST_NOW - 1),
            approval_bundle(("identity",), read_decision="REJECT"),
            approval_bundle(("identity",), exec_decision="REJECT"),
            approval_bundle(("identity",), same_approver=True),
            approval_bundle(("identity",), same_key=True),
            approval_bundle(("identity",), same_secret=True),
        ):
            with self.subTest(bundle=altered):
                with self.assertRaises(InvalidRequest):
                    self.run_candidate(("identity",), source, auth=altered)
        tampered_read = ReadSetApproval(**{**read_only.read_set.__dict__, "target_id": "other-target"})
        with self.assertRaises(InvalidRequest):
            self.run_candidate(("identity",), source,
                               auth=AuthorizationBundle(tampered_read, read_only.execution))
        with self.assertRaises(InvalidRequest):
            self.run_candidate(("identity",), source,
                               auth=approval_bundle(("identity",), auth_limits=limits(bytes_read=1)))
        with self.assertRaises(InvalidRequest):
            self.run_candidate(("identity",), source,
                               auth=approval_bundle(("identity",), budget_payload={"bytes_read": 1}))

    def test_authorization_replay_and_target_binding(self) -> None:
        source = FixtureSource()
        verifier = AuthorizationVerifier({TEST_KEY_ID: TEST_KEY, TEST_EXEC_KEY_ID: TEST_EXEC_KEY},
                                          clock=lambda: TEST_NOW)
        bundle = approval_bundle(("identity",))
        output, manifest, final_status = self.run_candidate(("identity",), source, auth=bundle,
                                                             verifier=verifier, acquisition_id="auth-first")
        output.close()
        self.assertEqual(final_status, ResultStatus.COMPLETE)
        with self.assertRaises(InvalidRequest):
            self.run_candidate(("identity",), FixtureSource(), auth=bundle,
                               verifier=verifier, acquisition_id="auth-replay")
        with self.assertRaises(InvalidRequest):
            orchestrator(target="different-vm").run(
                self.parent, authorization=approval_bundle(("identity",)),
                requested_modules=("identity",), source=FixtureSource(), acquisition_id="wrong-target")
        self.assertEqual(manifest["scope_authorization"]["target_id"], TEST_TARGET)

    def test_unselected_modules_are_not_called_and_unauthorized_modules_fail(self) -> None:
        source = FixtureSource()
        output, manifest, final_status = self.run_candidate(("identity",), source,
                                                             acquisition_id="identity-only")
        try:
            self.assertEqual(final_status, ResultStatus.COMPLETE)
            self.assertEqual(source.calls, ["identity"])
            self.assertEqual(manifest["module_results"]["systemd-state"]["status"], "NOT_REQUESTED")
            with self.assertRaises(InvalidRequest):
                orchestrator().run(self.parent, authorization=approval_bundle(("identity",)),
                                   requested_modules=("journal",), source=FixtureSource(),
                                   acquisition_id="unauthorized-journal")
        finally:
            output.close()

    def test_identity_schema_and_sensitive_extra_fields(self) -> None:
        output, manifest, final_status = self.run_candidate(("identity",), FixtureSource(),
                                                             acquisition_id="identity-valid")
        try:
            self.assertEqual(final_status, ResultStatus.COMPLETE)
            raw = (self.root / "alpha-m1-v2-identity-valid" / "module-identity.json").read_bytes()
            self.assertIn(b"system_id", raw)
            self.assertNotIn(b"cmdline", raw)
            self.assertEqual(manifest["budget_usage"]["bytes_read"], fixture_size(VALID_IDENTITY))
        finally:
            output.close()
        marker = "SYNTHETIC_UNEXPECTED_SECRET_aa91"
        bad = dict(VALID_IDENTITY, unexpected=marker)
        output, manifest, final_status = self.run_candidate(("identity",), FixtureSource(identity=bad),
                                                             acquisition_id="identity-extra")
        try:
            self.assertEqual(final_status, ResultStatus.PARSE_ERROR)
            raw = b"".join(path.read_bytes() for path in (self.root / "alpha-m1-v2-identity-extra").iterdir())
            self.assertNotIn(marker.encode(), raw)
        finally:
            output.close()

    def test_interruption_stops_dispatch_of_later_selected_modules(self) -> None:
        class InterruptedIdentity(FixtureSource):
            def read_identity(self, budget):
                self.calls.append("identity")
                raise KeyboardInterrupt()
        source = InterruptedIdentity()
        event = threading.Event()
        output, manifest, status = self.run_candidate(
            ("identity", "systemd-state"), source, acquisition_id="interrupt-stops",
            interrupt_event=event,
        )
        try:
            self.assertEqual(status, ResultStatus.INCONCLUSIVE)
            self.assertEqual(source.calls, ["identity"])
            self.assertEqual(manifest["module_results"]["systemd-state"]["status"], "INCONCLUSIVE")
            self.assertEqual(manifest["module_results"]["identity"]["status"], "INCONCLUSIVE")
        finally:
            output.close()

    @staticmethod
    def unit_record(unit_id: str, **changes):
        row = {
            "unit_id": unit_id, "LoadState": "loaded", "ActiveState": "active",
            "SubState": "running", "MainPID": 0,
            "ControlGroup": f"/system.slice/{unit_id}", "Result": "success",
            "ExecMainCode": 0, "ExecMainStatus": 0,
        }
        row.update(changes)
        return row

    def test_systemd_allowlist_duplicates_missing_properties_and_33_units(self) -> None:
        unit = alpha_unit_id(1)
        nested = self.unit_record(unit, ControlGroup=f"/system.slice/{unit}/worker.slice")
        output, manifest, status = self.run_candidate(("systemd-state",),
            FixtureSource(units=[nested]), acquisition_id="unit-valid")
        try:
            self.assertEqual(status, ResultStatus.COMPLETE)
            self.assertEqual(manifest["module_results"]["systemd-state"]["records"], 1)
            raw = (self.root / "alpha-m1-v2-unit-valid" / "module-systemd-state.json").read_bytes()
            self.assertNotIn(b"worker.slice", raw)
            self.assertNotIn(b"/system.slice/", raw)
            unit_output = json.loads(raw)
            self.assertTrue(unit_output["summary"]["units"][0]["properties"]["ControlGroup"])
        finally:
            output.close()

        for name, rows, expected in (
            ("unit-duplicate", [self.unit_record(unit), self.unit_record(unit)], ResultStatus.PARSE_ERROR),
            ("unit-foreign", [{"unit_id": "sshd.service"}], ResultStatus.PARSE_ERROR),
            ("unit-unknown-alpha", [{"unit_id": "athena-alpha-m1-000000000000000000000001.service"}], ResultStatus.PARSE_ERROR),
            ("unit-cgroup-prefix", [self.unit_record(unit, ControlGroup=f"/system.slice/{unit}evil")], ResultStatus.PARSE_ERROR),
            ("unit-missing", [{"unit_id": unit, "LoadState": "loaded"}], ResultStatus.INCONCLUSIVE),
        ):
            output, _, status = self.run_candidate(("systemd-state",), FixtureSource(units=rows),
                                                    acquisition_id=name)
            try:
                self.assertEqual(status, expected)
            finally:
                output.close()

        many = [self.unit_record(alpha_unit_id(i)) for i in range(33)]
        cap_limits = limits(units=32, records=128, bytes_read=2 * 1024 * 1024,
                            bytes_per_source=2 * 1024 * 1024)
        output, manifest, status = self.run_candidate(("systemd-state",), FixtureSource(units=many),
                                                       budget_limits=cap_limits, acquisition_id="unit-33")
        try:
            self.assertEqual(status, ResultStatus.TRUNCATED)
            self.assertEqual(manifest["budget_usage"]["units_enumerated"], 32)
            self.assertEqual(manifest["budget_usage"]["limits_reached"]["enumerated_unit"], 1)
        finally:
            output.close()

    def process_fixture(self, count: int, *, alpha_count: int | None = None):
        alpha_count = count if alpha_count is None else alpha_count
        refs = []
        snapshots = {}
        cgroups = {}
        for index in range(count):
            pid = 1000 + index
            ref = ProcessReference(pid, f"start-{pid}-1", "fixture-epoch-01")
            refs.append(ref)
            snapshots[pid] = {"pid": pid, "ppid": 1, "uid": 1000, "state": "S",
                              "source_id": ref.source_id, "snapshot_id": ref.snapshot_id}
            cgroups[pid] = (f"/system.slice/{alpha_unit_id(index % 32)}" if index < alpha_count
                            else "/user.slice/user-1000.slice/session-1.scope")
        return refs, snapshots, cgroups

    def test_process_cgroup_reconciliation_no_argv_and_complete_absence(self) -> None:
        refs, snapshots, cgroups = self.process_fixture(2, alpha_count=1)
        source = FixtureSource(process_refs=refs, process_snapshots=snapshots, cgroups=cgroups)
        output, manifest, status = self.run_candidate(("process-cgroups",),
            source,
            acquisition_id="process-mixed")
        try:
            self.assertEqual(status, ResultStatus.COMPLETE)
            summary = manifest["module_results"]["process-cgroups"]["artifact"]
            raw = (self.root / "alpha-m1-v2-process-mixed" / "module-process-cgroups.json").read_bytes()
            self.assertNotIn(b"argv", raw)
            self.assertNotIn(b"cmdline", raw)
            self.assertNotIn(b"process_name", raw)
            self.assertNotIn(b"source_id\":\"start-", raw)
            self.assertIn(b"source_id_sha256", raw)
            self.assertIn(b"unit_id", raw)
            self.assertNotIn(b"/system.slice/", raw)
            self.assertNotIn("process:1001", source.calls)
        finally:
            output.close()

        refs, snapshots, cgroups = self.process_fixture(2, alpha_count=0)
        output, manifest, status = self.run_candidate(("process-cgroups",),
            FixtureSource(process_refs=refs, process_snapshots=snapshots, cgroups=cgroups),
            acquisition_id="process-absence")
        try:
            self.assertEqual(status, ResultStatus.COMPLETE)
            raw = json.loads((self.root / "alpha-m1-v2-process-absence" / "module-process-cgroups.json").read_bytes())
            self.assertTrue(raw["summary"]["absence_demonstrated"])
            self.assertRegex(raw["summary"]["snapshot_id_sha256"], r"^[0-9a-f]{64}$")
        finally:
            output.close()

    def test_process_pid_reuse_missing_and_denied_cgroups_are_nonpositive(self) -> None:
        refs, snapshots, cgroups = self.process_fixture(1)
        snapshots[refs[0].pid] = dict(snapshots[refs[0].pid], source_id="reused-start")
        output, _, status = self.run_candidate(("process-cgroups",),
            FixtureSource(process_refs=refs, process_snapshots=snapshots, cgroups=cgroups),
            acquisition_id="process-reused")
        try:
            self.assertEqual(status, ResultStatus.INCONCLUSIVE)
        finally:
            output.close()

        class ChangedEpochSource(FixtureSource):
            def read_cgroup(self, reference, budget):
                membership = super().read_cgroup(reference, budget)
                return CgroupMembership(membership.pid, membership.source_id,
                                        "different-epoch", membership.cgroup)
        refs, snapshots, cgroups = self.process_fixture(1, alpha_count=0)
        output, manifest, status = self.run_candidate(("process-cgroups",),
            ChangedEpochSource(process_refs=refs, process_snapshots=snapshots, cgroups=cgroups),
            acquisition_id="process-epoch-changed")
        try:
            self.assertEqual(status, ResultStatus.INCONCLUSIVE)
            raw = json.loads((self.root / "alpha-m1-v2-process-epoch-changed" / "module-process-cgroups.json").read_bytes())
            self.assertFalse(raw["summary"]["absence_demonstrated"])
        finally:
            output.close()

        class MixedInventoryEpochSource(FixtureSource):
            def enumerate_processes(self, maximum, budget):
                page = super().enumerate_processes(maximum, budget)
                def mixed_refs():
                    for ref in page.items:
                        yield ProcessReference(ref.pid, ref.source_id, "other-inventory-epoch")
                return EnumerationPage(mixed_refs(), page.complete, snapshot_id=page.snapshot_id)
        refs, snapshots, cgroups = self.process_fixture(1, alpha_count=0)
        mixed_source = MixedInventoryEpochSource(process_refs=refs, process_snapshots=snapshots, cgroups=cgroups)
        output, _, status = self.run_candidate(("process-cgroups",), mixed_source,
                                                acquisition_id="process-ref-epoch-mismatch")
        try:
            self.assertEqual(status, ResultStatus.INCONCLUSIVE)
            self.assertFalse(any(call.startswith("cgroup:") for call in mixed_source.calls))
        finally:
            output.close()
        refs, snapshots, _ = self.process_fixture(1)
        output, _, status = self.run_candidate(("process-cgroups",),
            FixtureSource(process_refs=refs, process_snapshots=snapshots, cgroups={}),
            acquisition_id="process-no-cgroup")
        try:
            self.assertEqual(status, ResultStatus.INCONCLUSIVE)
        finally:
            output.close()

        class DeniedCgroupSource(FixtureSource):
            def read_cgroup(self, reference, budget):
                raise PermissionError("SYNTHETIC_PRIVATE_ERROR")
        refs, snapshots, cgroups = self.process_fixture(1)
        output, manifest, status = self.run_candidate(("process-cgroups",),
            DeniedCgroupSource(process_refs=refs, process_snapshots=snapshots, cgroups=cgroups),
            acquisition_id="process-denied")
        try:
            self.assertEqual(status, ResultStatus.ACCESS_DENIED)
            raw = b"".join(path.read_bytes() for path in (self.root / "alpha-m1-v2-process-denied").iterdir())
            self.assertNotIn(b"SYNTHETIC_PRIVATE_ERROR", raw)
        finally:
            output.close()

    def test_4097_pid_and_129_alpha_process_limits(self) -> None:
        refs, snapshots, cgroups = self.process_fixture(4097, alpha_count=0)
        large_limits = limits(records=8192, processes=4096, bytes_read=4 * 1024 * 1024,
                              bytes_per_source=4 * 1024 * 1024)
        output, manifest, status = self.run_candidate(("process-cgroups",),
            FixtureSource(process_refs=refs, process_snapshots=snapshots, cgroups=cgroups),
            budget_limits=large_limits, acquisition_id="pids-4097")
        try:
            self.assertEqual(status, ResultStatus.TRUNCATED)
            self.assertEqual(manifest["budget_usage"]["pids_enumerated"], 4096)
        finally:
            output.close()

        refs, snapshots, cgroups = self.process_fixture(129)
        output, manifest, status = self.run_candidate(("process-cgroups",),
            FixtureSource(process_refs=refs, process_snapshots=snapshots, cgroups=cgroups),
            budget_limits=large_limits, acquisition_id="alpha-pids-129")
        try:
            self.assertEqual(status, ResultStatus.TRUNCATED)
            module = json.loads((self.root / "alpha-m1-v2-alpha-pids-129" / "module-process-cgroups.json").read_bytes())
            self.assertEqual(module["records"], 128)
        finally:
            output.close()

    def test_enumeration_interruption_and_error_are_counted_and_nonpositive(self) -> None:
        unit = alpha_unit_id(1)
        for exception, expected, acquisition in (
            (KeyboardInterrupt, ResultStatus.INCONCLUSIVE, "unit-enum-interrupted"),
            (PermissionError, ResultStatus.ACCESS_DENIED, "unit-enum-denied"),
        ):
            source = FixtureSource(units=[self.unit_record(unit)], unit_page_exception=exception)
            output, manifest, status = self.run_candidate(("systemd-state",), source,
                                                           acquisition_id=acquisition)
            try:
                self.assertEqual(status, expected)
                self.assertGreaterEqual(manifest["budget_usage"]["access_attempts"], 2)
                if exception is KeyboardInterrupt:
                    self.assertGreater(manifest["budget_usage"]["operations_interrupted"], 0)
                self.assertNotEqual(status, ResultStatus.COMPLETE)
            finally:
                output.close()

    def test_interruption_and_finalization_seal_failure_never_complete(self) -> None:
        source = FixtureSource()
        event = threading.Event()
        event.set()
        output, manifest, status = self.run_candidate(("identity",), source,
            acquisition_id="preinterrupted", interrupt_event=event)
        output.close()
        self.assertEqual(status, ResultStatus.INCONCLUSIVE)
        self.assertEqual(source.calls, [])

        with mock.patch("qualification.alpha_m1_collector_v2.safe_fs.os.write", side_effect=OSError(28, "ENOSPC")):
            # The running manifest cannot be persisted; adapters are never entered.
            source = FixtureSource()
            output, partial_manifest, initial_status = self.run_candidate(
                ("identity",), source, acquisition_id="running-enospc")
        try:
            self.assertEqual(initial_status, ResultStatus.INCONCLUSIVE)
            self.assertEqual(source.calls, [])
            self.assertEqual(partial_manifest["finalization_state"], "INITIAL_OUTPUT_INCOMPLETE")
            self.assertFalse((self.root / "alpha-m1-v2-running-enospc" / "manifest.final.json").exists())
        finally:
            output.close()

        original = __import__("qualification.alpha_m1_collector_v2.safe_fs", fromlist=["SafeDirectory"]).SafeDirectory.write_finalization_file
        def fail_seal(directory, name, data, *, budget, file_kind):
            if file_kind == "seal":
                raise OSError(28, "ENOSPC")
            return original(directory, name, data, budget=budget, file_kind=file_kind)
        with mock.patch("qualification.alpha_m1_collector_v2.safe_fs.SafeDirectory.write_finalization_file", fail_seal):
            output, manifest, status = self.run_candidate(("identity",), FixtureSource(),
                                                           acquisition_id="seal-enospc")
        try:
            self.assertEqual(status, ResultStatus.INCONCLUSIVE)
            raw = (self.root / "alpha-m1-v2-seal-enospc" / "manifest.final.json").read_bytes()
            self.assertEqual(json.loads(raw)["completion"], ResultStatus.INCONCLUSIVE.value)
            self.assertEqual((self.root / "alpha-m1-v2-seal-enospc" / "manifest.final.sha256.json").exists(), False)
        finally:
            output.close()

    def test_manifest_serialization_fallback_and_integrity_verifier(self) -> None:
        import qualification.alpha_m1_collector_v2.orchestrator as orchestrator_module
        real_serialize = orchestrator_module._serialize_manifest
        injected = [False]
        def fail_once(value, maximum):
            if not injected[0]:
                injected[0] = True
                raise ParseFailure("synthetic serialization failure")
            return real_serialize(value, maximum)
        with mock.patch("qualification.alpha_m1_collector_v2.orchestrator._serialize_manifest",
                        side_effect=fail_once):
            output, manifest, status = self.run_candidate(("identity",), FixtureSource(),
                                                           acquisition_id="serialize-fallback")
        try:
            self.assertEqual(status, ResultStatus.INCONCLUSIVE)
            final = (self.root / "alpha-m1-v2-serialize-fallback" / "manifest.final.json").read_bytes()
            seal = (self.root / "alpha-m1-v2-serialize-fallback" / "manifest.final.sha256.json").read_bytes()
            self.assertEqual(json.loads(final)["errors"], ["MANIFEST_SERIALIZATION_FAILED"])
            self.assertEqual(verify_finalization(final, seal), ResultStatus.INCONCLUSIVE)
            tampered = final + b" "
            self.assertEqual(verify_finalization(tampered, seal), ResultStatus.INCONCLUSIVE)
        finally:
            output.close()

    def test_unbounded_module_iterable_and_global_budget_hard_max(self) -> None:
        yielded = [0]
        def unbounded():
            while True:
                yielded[0] += 1
                yield "identity"
        with self.assertRaises(InvalidRequest):
            orchestrator().run(self.parent, authorization=approval_bundle(("identity",)),
                               requested_modules=unbounded(), source=FixtureSource())
        self.assertEqual(yielded[0], 9)
        with self.assertRaises(InvalidRequest):
            CollectorOrchestrator(limits=BudgetLimits(bytes_read=999_999_999))

    def test_incomplete_statuses_never_aggregate_to_complete(self) -> None:
        cases = (
            (BudgetExhausted("cap"), ResultStatus.TRUNCATED),
            (PermissionError("denied"), ResultStatus.ACCESS_DENIED),
            (FileNotFoundError("absent"), ResultStatus.ABSENT),
            (TimeoutError("timed out"), ResultStatus.TIMEOUT),
        )
        from qualification.alpha_m1_collector_v2.contracts import ParseFailure
        cases += ((ParseFailure("bad syntax"), ResultStatus.PARSE_ERROR),)
        for error, expected in cases:
            with self.subTest(status=expected):
                actual = _status_for_exception(error)
                self.assertEqual(actual, expected)
                rows = {"identity": {"status": actual.value}}
                self.assertEqual(_aggregate_status(rows, ("identity",)), expected)
                self.assertNotEqual(actual, ResultStatus.COMPLETE)


if __name__ == "__main__":
    unittest.main(verbosity=2)
