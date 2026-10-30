"""Versioned evidence contract, module allowlist, and fixed hard budgets."""
from __future__ import annotations

from dataclasses import asdict, dataclass
from enum import Enum
import re
from typing import Any

from . import CANDIDATE_ID, SCHEMA_VERSION


class ResultStatus(str, Enum):
    COMPLETE = "COMPLETE"
    TRUNCATED = "TRUNCATED"
    ACCESS_DENIED = "ACCESS_DENIED"
    ABSENT = "ABSENT"
    PARSE_ERROR = "PARSE_ERROR"
    TIMEOUT = "TIMEOUT"
    INCONCLUSIVE = "INCONCLUSIVE"
    NOT_REQUESTED = "NOT_REQUESTED"


class CollectorError(Exception):
    status = ResultStatus.INCONCLUSIVE


class InvalidRequest(CollectorError):
    pass


class BudgetExhausted(CollectorError):
    status = ResultStatus.TRUNCATED


class DeadlineExceeded(CollectorError):
    status = ResultStatus.TIMEOUT


class UnsafePath(CollectorError):
    pass


class ParseFailure(CollectorError):
    status = ResultStatus.PARSE_ERROR


class Interrupted(CollectorError):
    status = ResultStatus.INCONCLUSIVE


class ConcurrentUse(CollectorError):
    """The single-threaded budget manager was used from another thread."""


@dataclass(frozen=True)
class BudgetLimits:
    bytes_read: int = 576 * 1024 * 1024
    bytes_persisted: int = 4 * 1024 * 1024
    files: int = 256
    records: int = 8192
    processes: int = 4096
    units: int = 32
    duration_seconds: int = 180
    bytes_per_file: int = 128 * 1024 * 1024
    bytes_per_source: int = 256 * 1024 * 1024
    output_file_bytes: int = 64 * 1024
    manifest_bytes: int = 64 * 1024
    seal_bytes: int = 4 * 1024


HARD_LIMITS = BudgetLimits()
DEFAULT_LIMITS = BudgetLimits()
MODULE_IDS = (
    "identity", "systemd-state", "process-cgroups", "paths-metadata",
    "build-integrity", "journal", "receipt-hash", "backup-hash",
)
MINIMUM_PROFILE = (
    "identity", "systemd-state", "process-cgroups", "paths-metadata", "build-integrity",
)
OPTIONAL_MODULES = frozenset({"journal", "receipt-hash", "backup-hash"})
MODULE_ID_RE = re.compile(r"[a-z][a-z0-9-]{0,47}\Z")

# These contracts are descriptive and are not executable collectors. In this
# phase every adapter is a non-reading placeholder and reports INCONCLUSIVE.
MODULE_SCOPES: dict[str, dict[str, Any]] = {
    "identity": {"reads": ["identity source contract fields"], "sensitive": True},
    "systemd-state": {"reads": ["fixed Alpha unit allowlist and selected properties"], "sensitive": False},
    "process-cgroups": {"reads": ["PID inventory, PPID/UID/state and cgroup membership only"], "sensitive": True},
    "paths-metadata": {"reads": ["fixed path metadata only; D6 payload excluded"], "sensitive": True},
    "build-integrity": {"reads": ["fixed manifest, generated inventory, artifact hashes"], "sensitive": True},
    "journal": {"reads": ["bounded selected Alpha journal records"], "sensitive": True},
    "receipt-hash": {"reads": ["one exact bounded receipt file for SHA-256"], "sensitive": True},
    "backup-hash": {"reads": ["bounded allowlisted backup metadata files for SHA-256"], "sensitive": True},
}


def validate_limits(limits: BudgetLimits) -> BudgetLimits:
    """Allow only integer limits no wider than the code-fixed hard maxima."""
    if not isinstance(limits, BudgetLimits):
        raise InvalidRequest("budget limits must use the fixed BudgetLimits contract")
    for key, hard in asdict(HARD_LIMITS).items():
        value = getattr(limits, key)
        if isinstance(value, bool) or not isinstance(value, int) or value < 0 or value > hard:
            raise InvalidRequest(f"budget {key} must be an integer in [0, {hard}]")
    return limits


def new_manifest(acquisition_id: str, selected: tuple[str, ...], limits: BudgetLimits,
                 started_monotonic_ns: int) -> dict[str, Any]:
    if not MODULE_ID_RE.fullmatch(acquisition_id) and not re.fullmatch(
        r"[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}", acquisition_id
    ):
        raise InvalidRequest("acquisition_id must be a canonical UUID or safe acquisition token")
    return {
        "schema_version": SCHEMA_VERSION,
        "candidate_id": CANDIDATE_ID,
        "acquisition_id": acquisition_id,
        "started_monotonic_ns": started_monotonic_ns,
        "scope_authorization": {
            "authorization_reference": None,
            "selected_modules": list(selected),
            "module_scopes": {name: MODULE_SCOPES[name] for name in selected},
            "note": "Candidate core only; no real collection capability is implemented.",
        },
        "budget_limits": asdict(limits),
        "budget_usage": {},
        "byte_counts": {"read": 0, "persisted": 0},
        "record_count": 0,
        "module_results": {},
        "duration_ns": 0,
        "completion": ResultStatus.INCONCLUSIVE.value,
        "errors": [],
        "anomalies": [],
        "artifacts": [],
        "manifest_digest_note": "manifest.final.json is excluded from its own artifact digest list",
    }
