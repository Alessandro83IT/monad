"""Minimized diagnostic adapters over an abstract, non-system source interface.

This module intentionally contains no Linux/systemd/proc backend. Adapters are
trusted code and receive only an AdapterBudget facade; test sources are pure
in-memory fixtures.
"""
from __future__ import annotations

from dataclasses import asdict, dataclass, is_dataclass
import hashlib
import json
import re
from typing import Any, Iterable, Mapping, Protocol, TypeVar
import uuid

from .budget import AdapterBudget
from .contracts import BudgetExhausted, ParseFailure, ResultStatus
from .results import ModuleResult

MAX_ALPHA_UNITS = 32
MAX_ENUMERATED_PIDS = 4096
MAX_ALPHA_PROCESSES = 128
MAX_SOURCE_RECORD_BYTES = 16 * 1024
UNIT_ID_RE = re.compile(r"athena-alpha-m1-[0-9a-f]{24}\.service\Z")
CGROUP_RE = re.compile(r"/system\.slice/athena-alpha-m1-[0-9a-f]{24}\.service(?:/[A-Za-z0-9_.@-]+)*\Z")
SAFE_CGROUP_PATH_RE = re.compile(r"/(?:[A-Za-z0-9_.@-]+)(?:/[A-Za-z0-9_.@-]+)*\Z")
OPAQUE_SOURCE_ID_RE = re.compile(r"[A-Za-z0-9._:-]{1,128}\Z")
SYSTEM_ID_RE = re.compile(r"[0-9a-f]{32}\Z")
KERNEL_RE = re.compile(r"[A-Za-z0-9._+()~-]{1,128}\Z")
TEXT_RE = re.compile(r"[A-Za-z0-9._+ ():/~-]{1,128}\Z")
VIRTUALIZATION = frozenset({"oracle", "kvm", "vmware", "hyperv", "bare-metal", "unknown"})

IDENTITY_FIELDS = frozenset({
    "system_id", "kernel", "distribution", "virtualization", "boot_id",
    "acquisition_environment_id",
})
UNIT_FIELDS = frozenset({
    "unit_id", "LoadState", "ActiveState", "SubState", "MainPID",
    "ControlGroup", "Result", "ExecMainCode", "ExecMainStatus",
})
PROCESS_REF_FIELDS = frozenset({"pid", "source_id", "snapshot_id"})
PROCESS_FIELDS = frozenset({"pid", "ppid", "uid", "state", "source_id", "snapshot_id"})
PROCESS_OUTPUT_FIELDS = frozenset({"pid", "ppid", "uid", "state", "source_id_sha256"})

# Exact service IDs documented by the historical Alpha inventory. Pattern
# matching alone would admit arbitrary services in the same naming namespace.
ALPHA_UNIT_IDS = frozenset({
    "athena-alpha-m1-28cc5aa5ec9a7fd1e7c6b715.service",
    "athena-alpha-m1-2ba07ed2f4ad03055baff910.service",
    "athena-alpha-m1-40ca148184e8131b74789e21.service",
    "athena-alpha-m1-43ff39862d99ed8e68d729ae.service",
    "athena-alpha-m1-45f1c7159a426941d20fed8a.service",
    "athena-alpha-m1-5c40304f1dae004fc15e3830.service",
    "athena-alpha-m1-6633d84535fa91142caf6bd5.service",
    "athena-alpha-m1-666aa836f5d15842d47bdef3.service",
    "athena-alpha-m1-716ab14bcbb4b154f5953042.service",
    "athena-alpha-m1-7fe9a30496fd95c631ea7c4b.service",
    "athena-alpha-m1-88fb1f423d5274d96d5ce40b.service",
    "athena-alpha-m1-958addae253edbe229d4917b.service",
    "athena-alpha-m1-c720b53df3240534303448a8.service",
    "athena-alpha-m1-fc331e22979bcd4126863547.service",
    "athena-alpha-m1-ff5c72902fe3a6513080384a.service",
})


@dataclass(frozen=True)
class EnumerationPage:
    items: Iterable[Any]
    complete: bool
    snapshot_id: str | None = None


@dataclass(frozen=True)
class ProcessReference:
    pid: int
    source_id: str
    snapshot_id: str


@dataclass(frozen=True)
class CgroupMembership:
    pid: int
    source_id: str
    snapshot_id: str
    cgroup: str | None


class DiagnosticSource(Protocol):
    """Trusted source API; implementations own byte charging before returning data.

    `record_source_bytes` accounts physical bytes consumed by the source. Adapter
    code separately validates the returned structure size, without charging it
    a second time. Implementations must request `read_allowance` before reads.
    """

    def read_identity(self, budget: AdapterBudget) -> Mapping[str, Any]: ...
    def enumerate_units(self, maximum: int, budget: AdapterBudget) -> EnumerationPage: ...
    def read_unit(self, unit_id: str, budget: AdapterBudget) -> Mapping[str, Any] | None: ...
    def enumerate_processes(self, maximum: int, budget: AdapterBudget) -> EnumerationPage: ...
    def read_process(self, reference: ProcessReference, budget: AdapterBudget) -> Mapping[str, Any] | None: ...
    def read_cgroup(self, reference: ProcessReference, budget: AdapterBudget) -> CgroupMembership | None: ...


def _encoded_size(value: Any) -> int:
    try:
        if is_dataclass(value):
            value = asdict(value)
        return len(json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("ascii"))
    except (TypeError, ValueError, RecursionError) as exc:
        raise ParseFailure("source record is not bounded structured data") from exc


def _read_source(source: DiagnosticSource, method: str, kind: str, budget: AdapterBudget,
                 *args: Any) -> Any:
    budget.check_deadline()
    budget.access_attempt(kind)
    try:
        value = getattr(source, method)(*args, budget)
        size = _encoded_size(value)
        if size > MAX_SOURCE_RECORD_BYTES:
            budget.mark_limit("bytes_per_file")
            raise BudgetExhausted("structured source record exceeds fixed size cap")
    except PermissionError:
        budget.access_denied_event(kind)
        raise
    except (InterruptedError, KeyboardInterrupt):
        budget.operation_interrupted(kind)
        raise
    except BaseException:
        # Do not let source exceptions or payload text enter the persisted report.
        budget.access_error(kind)
        raise
    budget.access_success(kind)
    return value


T = TypeVar("T")


def _read_page(source: DiagnosticSource, method: str, kind: str, item_kind: str,
               maximum: int, budget: AdapterBudget
               ) -> tuple[list[Any], bool, ResultStatus | None, list[str], str | None]:
    items: list[Any] = []
    errors: list[str] = []
    budget.access_attempt(kind)
    try:
        page = getattr(source, method)(maximum, budget)
        if not isinstance(page, EnumerationPage) or not isinstance(page.complete, bool):
            raise ParseFailure("enumeration source returned an invalid page contract")
        if (page.snapshot_id is not None and
                not _safe_text(page.snapshot_id, OPAQUE_SOURCE_ID_RE)):
            raise ParseFailure("enumeration snapshot ID is invalid")
        iterator = iter(page.items)
    except PermissionError:
        budget.access_denied_event(kind)
        return items, False, ResultStatus.ACCESS_DENIED, ["ACCESS_DENIED"], None
    except BudgetExhausted:
        budget.access_error(kind)
        return items, False, ResultStatus.TRUNCATED, ["TRUNCATED"], None
    except (InterruptedError, KeyboardInterrupt):
        budget.operation_interrupted(kind)
        return items, False, ResultStatus.INCONCLUSIVE, ["INTERRUPTED"], None
    except BaseException:
        budget.access_error(kind)
        return items, False, ResultStatus.PARSE_ERROR, ["PARSE_ERROR"], None
    budget.access_success(kind)

    # Never request more than `maximum` items. The source must signal an
    # additional page with complete=False; adapters do not probe item maximum+1.
    while len(items) < maximum:
        budget.access_attempt(kind)
        try:
            item = next(iterator)
        except StopIteration:
            budget.access_success(kind)
            if not page.complete:
                budget.mark_limit(f"enumerated_{item_kind}")
                return items, False, ResultStatus.TRUNCATED, ["ENUMERATION_LIMIT_REACHED"], page.snapshot_id
            return items, True, None, errors, page.snapshot_id
        except PermissionError:
            budget.access_denied_event(kind)
            return items, False, ResultStatus.ACCESS_DENIED, ["ACCESS_DENIED"], page.snapshot_id
        except BudgetExhausted:
            budget.access_error(kind)
            return items, False, ResultStatus.TRUNCATED, ["TRUNCATED"], page.snapshot_id
        except (InterruptedError, KeyboardInterrupt):
            budget.operation_interrupted(kind)
            return items, False, ResultStatus.INCONCLUSIVE, ["INTERRUPTED"], page.snapshot_id
        except BaseException:
            budget.access_error(kind)
            return items, False, ResultStatus.INCONCLUSIVE, ["ENUMERATION_INCOMPLETE"], page.snapshot_id
        budget.access_success(kind)
        try:
            encoded = _encoded_size(item)
            if encoded > MAX_SOURCE_RECORD_BYTES:
                budget.mark_limit("bytes_per_file")
                raise BudgetExhausted("enumeration item exceeds fixed size cap")
            budget.record_enumerated(item_kind)
        except BudgetExhausted:
            return items, False, ResultStatus.TRUNCATED, ["ENUMERATION_LIMIT_REACHED"], page.snapshot_id
        except BaseException:
            return items, False, ResultStatus.PARSE_ERROR, ["PARSE_ERROR"], page.snapshot_id
        items.append(item)

    if not page.complete:
        budget.mark_limit(f"enumerated_{item_kind}")
        return items, False, ResultStatus.TRUNCATED, ["ENUMERATION_LIMIT_REACHED"], page.snapshot_id
    return items, True, None, errors, page.snapshot_id


def _status_for_exception(exc: BaseException) -> ResultStatus:
    if isinstance(exc, BudgetExhausted):
        return ResultStatus.TRUNCATED
    if isinstance(exc, PermissionError):
        return ResultStatus.ACCESS_DENIED
    if isinstance(exc, FileNotFoundError):
        return ResultStatus.ABSENT
    if isinstance(exc, InterruptedError):
        return ResultStatus.INCONCLUSIVE
    if isinstance(exc, TimeoutError):
        return ResultStatus.TIMEOUT
    if isinstance(exc, ParseFailure):
        return ResultStatus.PARSE_ERROR
    return ResultStatus.INCONCLUSIVE


def _error_code(status: ResultStatus) -> str:
    return status.value


def _safe_text(value: Any, pattern: re.Pattern[str]) -> bool:
    return isinstance(value, str) and pattern.fullmatch(value) is not None


def _valid_identity(value: Any) -> dict[str, Any]:
    if not isinstance(value, Mapping) or set(value) != IDENTITY_FIELDS:
        raise ParseFailure("identity record fields are missing or unapproved")
    if not _safe_text(value["system_id"], SYSTEM_ID_RE):
        raise ParseFailure("system identifier format invalid")
    if not _safe_text(value["kernel"], KERNEL_RE):
        raise ParseFailure("kernel format invalid")
    if not _safe_text(value["distribution"], TEXT_RE):
        raise ParseFailure("distribution format invalid")
    if not isinstance(value["virtualization"], str) or value["virtualization"] not in VIRTUALIZATION:
        raise ParseFailure("virtualization value invalid")
    try:
        if str(uuid.UUID(str(value["boot_id"]))) != value["boot_id"]:
            raise ValueError
    except (ValueError, TypeError, AttributeError):
        raise ParseFailure("boot ID format invalid") from None
    if not _safe_text(value["acquisition_environment_id"], OPAQUE_SOURCE_ID_RE):
        raise ParseFailure("acquisition environment ID invalid")
    return {field: value[field] for field in sorted(IDENTITY_FIELDS)}


class IdentityAdapter:
    module_id = "identity"

    def collect(self, source: DiagnosticSource, budget: AdapterBudget) -> ModuleResult:
        try:
            value = _read_source(source, "read_identity", "identity-source", budget)
            normalized = _valid_identity(value)
            budget.consume_record("record")
            return ModuleResult(ResultStatus.COMPLETE, records=1,
                                summary={"identity": normalized})
        except BaseException as exc:
            interrupted = isinstance(exc, (InterruptedError, KeyboardInterrupt))
            return ModuleResult(_status_for_exception(exc), errors=("INTERRUPTED" if interrupted else _error_code(_status_for_exception(exc)),),
                                interrupted=interrupted)


def _valid_unit_id(value: Any) -> bool:
    return isinstance(value, str) and value in ALPHA_UNIT_IDS and UNIT_ID_RE.fullmatch(value) is not None


def _valid_unit_record(value: Mapping[str, Any], expected_id: str, *, source_record: bool = False
                       ) -> tuple[dict[str, Any], list[str]]:
    if set(value) - UNIT_FIELDS or value.get("unit_id") != expected_id:
        raise ParseFailure("unit record contains unapproved fields or ID")
    normalized: dict[str, Any] = {"unit_id": expected_id}
    missing: list[str] = []
    for field in sorted(UNIT_FIELDS - {"unit_id"}):
        field_value = value.get(field)
        if field not in value or field_value is None:
            normalized[field] = None
            missing.append(field)
        elif field == "MainPID":
            if isinstance(field_value, bool) or not isinstance(field_value, int) or not 0 <= field_value <= 2**31 - 1:
                raise ParseFailure("MainPID property invalid")
            normalized[field] = field_value
        elif field in {"ExecMainCode", "ExecMainStatus"}:
            if isinstance(field_value, bool) or not isinstance(field_value, int) or not -1 <= field_value <= 255:
                raise ParseFailure("ExecMain property invalid")
            normalized[field] = field_value
        elif field == "ControlGroup":
            if source_record:
                if not isinstance(field_value, str) or not CGROUP_RE.fullmatch(field_value):
                    raise ParseFailure("ControlGroup property invalid")
                expected_root = "/system.slice/" + expected_id
                if field_value != expected_root and not field_value.startswith(expected_root + "/"):
                    raise ParseFailure("ControlGroup does not match unit")
                normalized[field] = True
            elif field_value is True:
                normalized[field] = True
            else:
                raise ParseFailure("redacted ControlGroup presence flag invalid")
        else:
            if not _safe_text(field_value, TEXT_RE):
                raise ParseFailure(f"{field} property invalid")
            normalized[field] = field_value
    return normalized, missing


class SystemdStateAdapter:
    module_id = "systemd-state"

    def collect(self, source: DiagnosticSource, budget: AdapterBudget) -> ModuleResult:
        items, complete, page_status, errors, _snapshot_id = _read_page(
            source, "enumerate_units", "systemd-unit-list", "unit", MAX_ALPHA_UNITS, budget
        )
        if page_status is not None:
            return ModuleResult(page_status, errors=tuple(errors), interrupted="INTERRUPTED" in errors)
        records: list[dict[str, Any]] = []
        seen: set[str] = set()
        missing_any = False
        try:
            for unit_id in items:
                budget.check_deadline()
                if not _valid_unit_id(unit_id):
                    raise ParseFailure("unit ID is outside strict Alpha allowlist")
                if unit_id in seen:
                    raise ParseFailure("duplicate unit ID")
                seen.add(unit_id)
                value = _read_source(source, "read_unit", "systemd-unit-properties", budget, unit_id)
                if value is None:
                    missing_any = True
                    records.append({"unit_id": unit_id, "properties": {}, "missing_properties": sorted(UNIT_FIELDS - {"unit_id"})})
                    budget.consume_record("unit")
                    continue
                if not isinstance(value, Mapping):
                    raise ParseFailure("unit properties are not a mapping")
                normalized, missing = _valid_unit_record(value, unit_id, source_record=True)
                missing_any = missing_any or bool(missing)
                records.append({"unit_id": unit_id, "properties": normalized,
                                "missing_properties": missing})
                budget.consume_record("unit")
        except BaseException as exc:
            return ModuleResult(_status_for_exception(exc), records=len(records),
                                errors=(_error_code(_status_for_exception(exc)),),
                                anomalies=("PARTIAL_UNIT_INVENTORY",),
                                summary={"units": records, "inventory_complete": False},
                                interrupted=isinstance(exc, (InterruptedError, KeyboardInterrupt)))
        status = ResultStatus.INCONCLUSIVE if missing_any or not complete else ResultStatus.COMPLETE
        return ModuleResult(status, records=len(records),
                            errors=("UNIT_PROPERTIES_MISSING",) if missing_any else (),
                            summary={"units": records, "inventory_complete": complete})


def _pid(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and 1 <= value <= 2**31 - 1


def _valid_process_ref(value: Any) -> ProcessReference:
    if isinstance(value, ProcessReference):
        ref = value
    elif isinstance(value, Mapping) and set(value) == PROCESS_REF_FIELDS:
        ref = ProcessReference(value["pid"], value["source_id"], value["snapshot_id"])
    else:
        raise ParseFailure("process reference has invalid fields")
    if (not _pid(ref.pid) or not _safe_text(ref.source_id, OPAQUE_SOURCE_ID_RE) or
            not _safe_text(ref.snapshot_id, OPAQUE_SOURCE_ID_RE)):
        raise ParseFailure("process reference values invalid")
    return ref


def _valid_process_snapshot(value: Mapping[str, Any], ref: ProcessReference) -> dict[str, Any]:
    if set(value) != PROCESS_FIELDS:
        raise ParseFailure("process snapshot contains missing or unapproved fields")
    if (value["pid"] != ref.pid or value["source_id"] != ref.source_id or
            value["snapshot_id"] != ref.snapshot_id):
        raise RuntimeError("PID_REUSED")
    if not _pid(value["pid"]):
        raise ParseFailure("process PID invalid")
    if isinstance(value["ppid"], bool) or not isinstance(value["ppid"], int) or not 0 <= value["ppid"] <= 2**31 - 1:
        raise ParseFailure("process PPID invalid")
    if isinstance(value["uid"], bool) or not isinstance(value["uid"], int) or not 0 <= value["uid"] <= 2**32 - 1:
        raise ParseFailure("process UID invalid")
    if not isinstance(value["state"], str) or value["state"] not in {"R", "S", "D", "T", "Z", "I", "W", "X"}:
        raise ParseFailure("process state invalid")
    if not _safe_text(value["source_id"], OPAQUE_SOURCE_ID_RE):
        raise ParseFailure("process source ID invalid")
    return {
        "pid": value["pid"], "ppid": value["ppid"], "uid": value["uid"],
        "state": value["state"],
        "source_id_sha256": hashlib.sha256(value["source_id"].encode("ascii")).hexdigest(),
    }


def _unit_for_cgroup(cgroup: str) -> str | None:
    if not isinstance(cgroup, str) or not SAFE_CGROUP_PATH_RE.fullmatch(cgroup) or ".." in cgroup.split("/"):
        return None
    if not cgroup.startswith("/system.slice/"):
        return None
    suffix = cgroup[len("/system.slice/"):]
    unit_id = suffix.split("/", 1)[0]
    return unit_id if _valid_unit_id(unit_id) else None


def _valid_process_output(value: Mapping[str, Any]) -> dict[str, Any] | None:
    if set(value) != PROCESS_OUTPUT_FIELDS:
        return None
    if (not _pid(value["pid"]) or isinstance(value["ppid"], bool) or
            not isinstance(value["ppid"], int) or not 0 <= value["ppid"] <= 2**31 - 1 or
            isinstance(value["uid"], bool) or not isinstance(value["uid"], int) or
            not 0 <= value["uid"] <= 2**32 - 1 or
            not isinstance(value["state"], str) or value["state"] not in {"R", "S", "D", "T", "Z", "I", "W", "X"} or
            not isinstance(value["source_id_sha256"], str) or
            re.fullmatch(r"[0-9a-f]{64}", value["source_id_sha256"]) is None):
        return None
    return {field: value[field] for field in sorted(PROCESS_OUTPUT_FIELDS)}


class ProcessCgroupsAdapter:
    module_id = "process-cgroups"

    def collect(self, source: DiagnosticSource, budget: AdapterBudget) -> ModuleResult:
        refs_raw, inventory_complete, page_status, errors, inventory_snapshot = _read_page(
            source, "enumerate_processes", "pid-list", "pid", MAX_ENUMERATED_PIDS, budget
        )
        status = page_status or (ResultStatus.COMPLETE if inventory_complete else ResultStatus.INCONCLUSIVE)
        if not _safe_text(inventory_snapshot, OPAQUE_SOURCE_ID_RE):
            status = ResultStatus.INCONCLUSIVE
            errors.append("SNAPSHOT_ID_MISSING")
        process_records: list[dict[str, Any]] = []
        seen_pids: set[int] = set()
        all_membership_known = True
        try:
            for raw_ref in refs_raw:
                budget.check_deadline()
                ref = _valid_process_ref(raw_ref)
                if ref.pid in seen_pids:
                    raise ParseFailure("duplicate PID in process inventory")
                seen_pids.add(ref.pid)
                if ref.snapshot_id != inventory_snapshot:
                    status = ResultStatus.INCONCLUSIVE
                    all_membership_known = False
                    errors.append("PID_INVENTORY_EPOCH_MISMATCH")
                    continue
                # Membership is the least sensitive discriminator available.
                # Do not read PPID/UID/state/generation metadata for processes
                # outside the Alpha units.
                membership = _read_source(source, "read_cgroup", "cgroup-source", budget, ref)
                if membership is None:
                    status = ResultStatus.INCONCLUSIVE
                    all_membership_known = False
                    errors.append("CGROUP_ABSENT")
                    continue
                if not isinstance(membership, CgroupMembership):
                    raise ParseFailure("cgroup source returned an invalid snapshot")
                if (membership.pid != ref.pid or membership.source_id != ref.source_id or
                        membership.snapshot_id != inventory_snapshot):
                    status = ResultStatus.INCONCLUSIVE
                    all_membership_known = False
                    errors.append("PID_OR_SNAPSHOT_CHANGED")
                    continue
                cgroup = membership.cgroup
                if cgroup is None:
                    status = ResultStatus.INCONCLUSIVE
                    all_membership_known = False
                    errors.append("CGROUP_ABSENT")
                    continue
                if not isinstance(cgroup, str) or not SAFE_CGROUP_PATH_RE.fullmatch(cgroup) or ".." in cgroup.split("/"):
                    raise ParseFailure("cgroup membership format invalid")
                unit_id = _unit_for_cgroup(cgroup)
                if unit_id is None:
                    continue
                snapshot = _read_source(source, "read_process", "process-snapshot", budget, ref)
                if snapshot is None:
                    status = ResultStatus.INCONCLUSIVE
                    all_membership_known = False
                    errors.append("PID_DISAPPEARED")
                    continue
                if not isinstance(snapshot, Mapping):
                    raise ParseFailure("process snapshot is not a mapping")
                try:
                    process = _valid_process_snapshot(snapshot, ref)
                except RuntimeError:
                    status = ResultStatus.INCONCLUSIVE
                    all_membership_known = False
                    errors.append("PID_REUSED")
                    continue
                if len(process_records) >= MAX_ALPHA_PROCESSES:
                    budget.mark_limit("processes")
                    status = ResultStatus.TRUNCATED
                    errors.append("ALPHA_PROCESS_LIMIT_REACHED")
                    break
                budget.consume_record("process")
                process_records.append({**process, "unit_id": unit_id})
        except BaseException as exc:
            error_status = _status_for_exception(exc)
            if error_status == ResultStatus.TRUNCATED or status == ResultStatus.COMPLETE:
                status = error_status
            interrupted = isinstance(exc, (InterruptedError, KeyboardInterrupt))
            errors.append("INTERRUPTED" if interrupted else _error_code(error_status))
            all_membership_known = False

        if page_status is not None and page_status != ResultStatus.COMPLETE:
            status = page_status
        complete_absence = inventory_complete and all_membership_known and not process_records and status == ResultStatus.COMPLETE
        if not inventory_complete and status == ResultStatus.COMPLETE:
            status = ResultStatus.INCONCLUSIVE
        summary = {
            "processes": process_records,
            "inventory_complete": inventory_complete,
            "absence_demonstrated": complete_absence,
            "snapshot_id_sha256": (
                hashlib.sha256(inventory_snapshot.encode("ascii")).hexdigest()
                if _safe_text(inventory_snapshot, OPAQUE_SOURCE_ID_RE) else None
            ),
        }
        return ModuleResult(status, records=len(process_records),
                            errors=tuple(errors), summary=summary,
                            interrupted="INTERRUPTED" in errors)


def safe_adapter_summary(module_id: str, summary: Mapping[str, Any]) -> dict[str, Any] | None:
    """Revalidate exact output schemas before they can reach JSON persistence."""
    if module_id == "identity":
        if set(summary) != {"identity"}:
            return None
        return {"identity": _valid_identity(summary["identity"])}
    if module_id == "systemd-state":
        if set(summary) != {"units", "inventory_complete"}:
            return None
        rows = summary["units"]
        if not isinstance(rows, list) or len(rows) > MAX_ALPHA_UNITS or not isinstance(summary["inventory_complete"], bool):
            return None
        clean_rows: list[dict[str, Any]] = []
        seen: set[str] = set()
        for row in rows:
            if not isinstance(row, Mapping) or set(row) != {"unit_id", "properties", "missing_properties"}:
                return None
            unit_id = row["unit_id"]
            if not _valid_unit_id(unit_id) or unit_id in seen:
                return None
            seen.add(unit_id)
            missing = row["missing_properties"]
            properties = row["properties"]
            if not isinstance(missing, list) or any(item not in UNIT_FIELDS - {"unit_id"} for item in missing):
                return None
            if not isinstance(properties, Mapping):
                return None
            if not properties:
                if set(missing) != UNIT_FIELDS - {"unit_id"}:
                    return None
                clean_props = {}
            else:
                clean_props, expected_missing = _valid_unit_record(properties, unit_id)
                if sorted(missing) != sorted(expected_missing):
                    return None
            clean_rows.append({"unit_id": unit_id, "properties": clean_props,
                               "missing_properties": sorted(missing)})
        return {"units": clean_rows, "inventory_complete": summary["inventory_complete"]}
    if module_id == "process-cgroups":
        if set(summary) != {"processes", "inventory_complete", "absence_demonstrated", "snapshot_id_sha256"}:
            return None
        rows = summary["processes"]
        complete = summary["inventory_complete"]
        absence = summary["absence_demonstrated"]
        snapshot_digest = summary["snapshot_id_sha256"]
        if (not isinstance(rows, list) or len(rows) > MAX_ALPHA_PROCESSES or
                not isinstance(complete, bool) or not isinstance(absence, bool) or
                (snapshot_digest is not None and
                 (not isinstance(snapshot_digest, str) or re.fullmatch(r"[0-9a-f]{64}", snapshot_digest) is None)) or
                (absence and (not complete or rows))):
            return None
        clean_rows: list[dict[str, Any]] = []
        seen: set[int] = set()
        for row in rows:
            required = PROCESS_OUTPUT_FIELDS | {"unit_id"}
            if not isinstance(row, Mapping) or set(row) != required:
                return None
            process = _valid_process_output({key: row[key] for key in PROCESS_OUTPUT_FIELDS})
            if process is None:
                return None
            if row["pid"] in seen or not _valid_unit_id(row["unit_id"]):
                return None
            seen.add(row["pid"])
            clean_rows.append({**process, "unit_id": row["unit_id"]})
        return {"processes": clean_rows, "inventory_complete": complete,
                "absence_demonstrated": absence, "snapshot_id_sha256": snapshot_digest}
    return None


ADAPTERS = {
    "identity": IdentityAdapter(),
    "systemd-state": SystemdStateAdapter(),
    "process-cgroups": ProcessCgroupsAdapter(),
}


def alpha_unit_id(index: int) -> str:
    """Return a documented Alpha ID (fixture indices wrap the fixed allowlist)."""
    if isinstance(index, bool) or not isinstance(index, int) or index < 0:
        raise ValueError("index must be a non-negative integer")
    values = sorted(ALPHA_UNIT_IDS)
    return values[index % len(values)]
