"""Single-threaded accounting with a protected finalization output reserve."""
from __future__ import annotations

from collections import Counter, defaultdict
from contextlib import contextmanager
from dataclasses import asdict
import threading
import time
from typing import Any, Iterator

from .contracts import (
    BudgetExhausted, BudgetLimits, ConcurrentUse, DeadlineExceeded, InvalidRequest,
    MODULE_IDS, validate_limits,
)

ACCESS_KINDS = frozenset({
    "file-open", "file-metadata", "file-read", "file-write", "file-fsync", "directory-fsync",
    "identity-source", "systemd-unit-list",
    "systemd-unit-properties", "pid-list", "process-snapshot", "cgroup-source",
})


class Budget:
    """Mutable, single-thread owner budget; adapter code receives only a view.

    The global output ceiling stays fixed. `manifest_bytes + seal_bytes` are
    ring-fenced from ordinary output writes and can be consumed only by the
    orchestrator's finalization path.
    """

    def __init__(self, limits: BudgetLimits = BudgetLimits(), *, clock=time.monotonic_ns):
        self.limits = validate_limits(limits)
        self.finalization_reserve = self.limits.manifest_bytes + self.limits.seal_bytes
        if self.limits.bytes_persisted < self.finalization_reserve:
            raise InvalidRequest("global persisted-byte budget is smaller than finalization reserve")
        self._clock = clock
        self.started_ns = clock()
        self._owner_thread: int | None = None
        self._owner_guard = threading.Lock()
        self.bytes_read = 0
        self.bytes_persisted = 0
        self.files_attempted = 0
        self.files_opened = 0
        self.files_enumerated = 0
        self.files_read = 0
        self.pids_enumerated = 0
        self.units_enumerated = 0
        self.records = 0
        self.processes = 0
        self.units = 0
        self.operations_interrupted = 0
        self.access_attempts = 0
        self.access_successes = 0
        self.access_denied = 0
        self.access_errors = 0
        self.limits_reached: Counter[str] = Counter()
        self.access_by_kind: dict[str, Counter[str]] = defaultdict(Counter)
        self.bytes_by_source: dict[str, int] = defaultdict(int)
        self.finalization_bytes = 0

    def _claim_thread(self) -> None:
        current = threading.get_ident()
        with self._owner_guard:
            if self._owner_thread is None:
                self._owner_thread = current
            elif self._owner_thread != current:
                raise ConcurrentUse("budget manager supports one thread only")

    @contextmanager
    def serial_session(self) -> Iterator[None]:
        """Claim this budget for the current thread; another thread is rejected."""
        self._claim_thread()
        yield

    def check_deadline(self) -> None:
        self._claim_thread()
        if self._clock() - self.started_ns > self.limits.duration_seconds * 1_000_000_000:
            self._limit("duration")
            raise DeadlineExceeded("global acquisition deadline exceeded")

    def _limit(self, name: str) -> None:
        self.limits_reached[name] += 1

    def note_interruption(self) -> None:
        self._claim_thread()
        self.operations_interrupted += 1

    def mark_limit(self, name: str) -> None:
        self._claim_thread()
        if name not in {"files", "bytes_read", "bytes_per_source", "bytes_per_file",
                        "bytes_persisted", "output_file_bytes", "manifest_file_bytes",
                        "seal_file_bytes", "records", "processes", "units",
                        "enumerated_file", "enumerated_pid", "enumerated_unit",
                        "duration", "adapter_output_reserve"}:
            raise InvalidRequest("unknown budget limit category")
        self._limit(name)

    def _finish_access(self, kind: str, terminal: str) -> None:
        row = self.access_by_kind[kind]
        finished = sum(row.get(name, 0) for name in ("succeeded", "denied", "errors", "interrupted"))
        if row.get("attempted", 0) <= finished:
            raise InvalidRequest("access completion has no matching attempt")
        row[terminal] += 1

    def access_attempt(self, kind: str) -> None:
        self._claim_thread()
        if kind not in ACCESS_KINDS:
            raise InvalidRequest("unknown access category")
        self.access_attempts += 1
        self.access_by_kind[kind]["attempted"] += 1

    def access_success(self, kind: str) -> None:
        self._claim_thread()
        if kind not in ACCESS_KINDS:
            raise InvalidRequest("unknown access category")
        self._finish_access(kind, "succeeded")
        self.access_successes += 1

    def access_denied_event(self, kind: str) -> None:
        self._claim_thread()
        if kind not in ACCESS_KINDS:
            raise InvalidRequest("unknown access category")
        self._finish_access(kind, "denied")
        self.access_denied += 1

    def access_error(self, kind: str) -> None:
        self._claim_thread()
        if kind not in ACCESS_KINDS:
            raise InvalidRequest("unknown access category")
        self._finish_access(kind, "errors")
        self.access_errors += 1

    def operation_interrupted(self, kind: str) -> None:
        self._claim_thread()
        if kind not in ACCESS_KINDS:
            raise InvalidRequest("unknown access category")
        self._finish_access(kind, "interrupted")
        self.operations_interrupted += 1

    def begin_input_open(self) -> None:
        self._claim_thread()
        self.files_attempted += 1
        if self.files_attempted > self.limits.files:
            self._limit("files")
            raise BudgetExhausted("file-count budget exhausted")
        self.access_attempt("file-open")

    def input_opened(self, source: str, *, declared_size: int, per_file_limit: int | None = None) -> None:
        self._claim_thread()
        if source not in MODULE_IDS:
            raise InvalidRequest("budget source must be a fixed module identifier")
        self.access_success("file-open")
        self.files_opened += 1
        cap = self.limits.bytes_per_file if per_file_limit is None else per_file_limit
        if isinstance(cap, bool) or not isinstance(cap, int) or cap < 0 or cap > self.limits.bytes_per_file:
            self._limit("bytes_per_file")
            raise BudgetExhausted("per-file limit exceeds fixed hard maximum")
        if declared_size < 0 or declared_size > cap:
            self._limit("bytes_per_file")
            raise BudgetExhausted("file exceeds per-file byte budget")

    def output_open_attempted(self) -> None:
        self._claim_thread()
        self.files_attempted += 1
        if self.files_attempted > self.limits.files:
            self._limit("files")
            raise BudgetExhausted("file-count budget exhausted")
        self.access_attempt("file-open")

    def output_opened(self) -> None:
        self._claim_thread()
        self.access_success("file-open")
        self.files_opened += 1

    def record_enumerated(self, kind: str, count: int = 1) -> None:
        self._claim_thread()
        if isinstance(count, bool) or not isinstance(count, int) or count < 0:
            raise ValueError("invalid enumeration count")
        if kind == "file":
            cap = self.limits.files
            actual = self.files_enumerated + count
        elif kind == "pid":
            cap = self.limits.processes
            actual = self.pids_enumerated + count
        elif kind == "unit":
            cap = self.limits.units
            actual = self.units_enumerated + count
        else:
            raise InvalidRequest("unknown enumeration category")
        if actual > cap:
            self._limit(f"enumerated_{kind}")
            raise BudgetExhausted(f"{kind}-enumeration budget exhausted")
        if kind == "file":
            self.files_enumerated = actual
        elif kind == "pid":
            self.pids_enumerated = actual
        else:
            self.units_enumerated = actual

    def record_file_read(self) -> None:
        self._claim_thread()
        self.files_read += 1

    def consume_read(self, amount: int, source: str) -> None:
        self._claim_thread()
        if source not in MODULE_IDS:
            raise InvalidRequest("budget source must be a fixed module identifier")
        if amount < 0:
            raise ValueError("negative read accounting")
        if self.bytes_read + amount > self.limits.bytes_read:
            self._limit("bytes_read")
            raise BudgetExhausted("global read-byte budget exhausted")
        if self.bytes_by_source[source] + amount > self.limits.bytes_per_source:
            self._limit("bytes_per_source")
            raise BudgetExhausted(f"read-byte budget exhausted for source {source}")
        self.bytes_read += amount
        self.bytes_by_source[source] += amount

    def read_allowance(self, source: str, maximum: int) -> int:
        self.check_deadline()
        if source not in MODULE_IDS:
            raise InvalidRequest("budget source must be a fixed module identifier")
        if isinstance(maximum, bool) or not isinstance(maximum, int) or maximum < 0:
            raise ValueError("invalid read request size")
        allowance = min(
            maximum,
            self.limits.bytes_read - self.bytes_read,
            self.limits.bytes_per_source - self.bytes_by_source[source],
        )
        if allowance <= 0 and maximum > 0:
            self._limit("bytes_read")
            raise BudgetExhausted("read-byte budget exhausted before source read")
        return allowance

    def record_source_bytes(self, amount: int, source: str) -> None:
        """Charge bytes consumed from a structured source adapter."""
        self._claim_thread()
        if source not in MODULE_IDS or isinstance(amount, bool) or not isinstance(amount, int) or amount < 0:
            raise InvalidRequest("invalid structured source byte accounting")
        self.bytes_read += amount
        self.bytes_by_source[source] += amount
        over_global = self.bytes_read > self.limits.bytes_read
        over_source = self.bytes_by_source[source] > self.limits.bytes_per_source
        if over_global or over_source:
            self._limit("bytes_read" if over_global else "bytes_per_source")
            raise BudgetExhausted("structured source exceeded its read-byte budget")

    def record_persisted(self, amount: int) -> None:
        """Ordinary output may use only bytes outside the finalization reserve."""
        self._claim_thread()
        adapter_ceiling = self.limits.bytes_persisted - self.finalization_reserve
        if amount < 0 or amount > self.limits.output_file_bytes:
            self._limit("output_file_bytes")
            raise BudgetExhausted("invalid or excessive output write size")
        if self.bytes_persisted + amount > adapter_ceiling:
            self._limit("adapter_output_reserve")
            raise BudgetExhausted("ordinary output reached protected finalization reserve")
        self.bytes_persisted += amount

    def record_finalization_persisted(self, amount: int, *, file_kind: str) -> None:
        self._claim_thread()
        if file_kind not in {"manifest", "seal"} or amount < 0:
            raise InvalidRequest("invalid finalization accounting request")
        cap = self.limits.manifest_bytes if file_kind == "manifest" else self.limits.seal_bytes
        used_key = f"_{file_kind}_bytes_used"
        used = getattr(self, used_key, 0)
        if used + amount > cap:
            self._limit(f"{file_kind}_bytes")
            raise BudgetExhausted(f"{file_kind} exceeded its fixed size limit")
        if self.bytes_persisted + amount > self.limits.bytes_persisted:
            self._limit("bytes_persisted")
            raise BudgetExhausted("global persisted-byte budget exhausted")
        self.bytes_persisted += amount
        self.finalization_bytes += amount
        setattr(self, used_key, used + amount)

    def consume_record(self, kind: str = "record", count: int = 1) -> None:
        self.check_deadline()
        if kind not in {"record", "process", "unit"}:
            raise ValueError("unknown record accounting category")
        if isinstance(count, bool) or not isinstance(count, int) or count < 0:
            raise ValueError("invalid record count")
        next_count = self.records + count
        if next_count > self.limits.records:
            self._limit("records")
            raise BudgetExhausted("record-count budget exhausted")
        if kind == "process" and self.processes + count > self.limits.processes:
            self._limit("processes")
            raise BudgetExhausted("process-count budget exhausted")
        if kind == "unit" and self.units + count > self.limits.units:
            self._limit("units")
            raise BudgetExhausted("unit-count budget exhausted")
        self.records = next_count
        if kind == "process":
            self.processes += count
        elif kind == "unit":
            self.units += count

    def snapshot(self) -> dict[str, Any]:
        self._claim_thread()
        duration = max(0, self._clock() - self.started_ns)
        if duration > self.limits.duration_seconds * 1_000_000_000:
            self._limit("duration")
        return {
            "limits": asdict(self.limits),
            "access_attempts": self.access_attempts,
            "access_successes": self.access_successes,
            "access_denied": self.access_denied,
            "access_errors": self.access_errors,
            "access_by_kind": {kind: dict(sorted(values.items())) for kind, values in sorted(self.access_by_kind.items())},
            "files_attempted": self.files_attempted,
            "files_opened": self.files_opened,
            "files_enumerated": self.files_enumerated,
            "files_read": self.files_read,
            "pids_enumerated": self.pids_enumerated,
            "units_enumerated": self.units_enumerated,
            "bytes_read": self.bytes_read,
            "bytes_read_by_source": dict(sorted(self.bytes_by_source.items())),
            "bytes_persisted": self.bytes_persisted,
            "finalization_reserve": self.finalization_reserve,
            "finalization_bytes": self.finalization_bytes,
            "record_count": self.records,
            "process_count": self.processes,
            "unit_count": self.units,
            "operations_interrupted": self.operations_interrupted,
            "limits_reached": dict(sorted(self.limits_reached.items())),
            "duration_ns": duration,
        }


class AdapterBudget:
    """Restricted adapter facade; deliberately has no output/finalization API."""

    __slots__ = ("__budget", "__source")

    def __init__(self, budget: Budget, module_id: str):
        if module_id not in MODULE_IDS:
            raise InvalidRequest("unknown adapter module ID")
        self.__budget = budget
        self.__source = module_id

    @property
    def limits(self) -> BudgetLimits:
        return self.__budget.limits

    def check_deadline(self) -> None:
        self.__budget.check_deadline()

    def access_attempt(self, kind: str) -> None:
        self.__budget.access_attempt(kind)

    def access_success(self, kind: str) -> None:
        self.__budget.access_success(kind)

    def access_denied_event(self, kind: str) -> None:
        self.__budget.access_denied_event(kind)

    def access_error(self, kind: str) -> None:
        self.__budget.access_error(kind)

    def operation_interrupted(self, kind: str) -> None:
        self.__budget.operation_interrupted(kind)

    def record_source_bytes(self, amount: int) -> None:
        self.__budget.record_source_bytes(amount, self.__source)

    def read_allowance(self, maximum: int) -> int:
        return self.__budget.read_allowance(self.__source, maximum)

    def record_enumerated(self, kind: str, count: int = 1) -> None:
        self.__budget.record_enumerated(kind, count)

    def mark_limit(self, name: str) -> None:
        self.__budget.mark_limit(name)

    def consume_record(self, kind: str = "record", count: int = 1) -> None:
        self.__budget.consume_record(kind, count)
