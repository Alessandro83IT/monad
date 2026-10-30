"""Fixed identity contract for the unpromoted Session 58 candidate."""
from __future__ import annotations

import hashlib
import re
from typing import Any, Mapping, Sequence

from . import CANDIDATE_ID
from .contracts import BudgetLimits, MODULE_IDS, validate_limits

SCHEMA_VERSION = 1
AUTHORIZATION_CONTRACT_VERSION = "alpha-m1-authz-v2"
MANIFEST_FILENAME = "candidate_identity.json"
ENTRYPOINT_PATH = "qualification/alpha_m1_collector_v2/synthetic_entry.py"
ENTRYPOINT_CALLABLE = "main"
READ_SET_HARD_MAXIMUM = ("identity", "systemd-state", "process-cgroups")
MAX_COMPONENT_BYTES = 1024 * 1024
MAX_TOTAL_COMPONENT_BYTES = 8 * 1024 * 1024
SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")

# This list is code-fixed. A manifest cannot add a module or replace the
# synthetic entry point by editing configuration alone.
AUTHORIZED_COMPONENT_PATHS = tuple(
    "qualification/alpha_m1_collector_v2/" + name
    for name in (
        "__init__.py", "aap_v1.py", "adapters.py", "authorization.py", "budget.py",
        "candidate_identity.py", "contracts.py", "nonce_ledger.py",
        "orchestrator.py", "results.py", "safe_fs.py", "synthetic_entry.py",
        "target_attestation.py", "trust_chain.py", "verified_launcher.py",
    )
)


def aggregate_digest(records: Sequence[Mapping[str, Any]]) -> str:
    """Hash a canonical, framed sequence of path, size, and file digest."""
    digest = hashlib.sha256(b"monad-alpha-m1-candidate-v1\0")
    for record in records:
        path = record["path"].encode("utf-8")
        size = record["bytes"]
        file_digest = bytes.fromhex(record["sha256"])
        digest.update(len(path).to_bytes(4, "big"))
        digest.update(path)
        digest.update(size.to_bytes(8, "big"))
        digest.update(file_digest)
    return digest.hexdigest()


def validate_identity_manifest(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != {
        "schema_version", "candidate_id", "authorization_contract_version",
        "components", "aggregate_sha256", "entrypoint", "read_set_maximum",
        "budget_maximum",
    }:
        raise ValueError("candidate identity manifest has an invalid schema")
    if (type(value["schema_version"]) is not int or value["schema_version"] != SCHEMA_VERSION or
            value["candidate_id"] != CANDIDATE_ID or
            value["authorization_contract_version"] != AUTHORIZATION_CONTRACT_VERSION):
        raise ValueError("candidate identity or contract version mismatch")
    components = value["components"]
    if not isinstance(components, list) or len(components) != len(AUTHORIZED_COMPONENT_PATHS):
        raise ValueError("candidate component count mismatch")
    total = 0
    for expected_path, row in zip(AUTHORIZED_COMPONENT_PATHS, components, strict=True):
        if not isinstance(row, dict) or set(row) != {"path", "bytes", "sha256"}:
            raise ValueError("candidate component record schema invalid")
        if row["path"] != expected_path:
            raise ValueError("candidate component list/order mismatch")
        if (isinstance(row["bytes"], bool) or not isinstance(row["bytes"], int) or
                not 0 < row["bytes"] <= MAX_COMPONENT_BYTES or
                not isinstance(row["sha256"], str) or not SHA256_RE.fullmatch(row["sha256"])):
            raise ValueError("candidate component size or digest invalid")
        total += row["bytes"]
    if total > MAX_TOTAL_COMPONENT_BYTES:
        raise ValueError("candidate aggregate component bytes exceed hard cap")
    aggregate = aggregate_digest(components)
    if value["aggregate_sha256"] != aggregate:
        raise ValueError("candidate aggregate digest mismatch")
    if value["entrypoint"] != {"path": ENTRYPOINT_PATH, "callable": ENTRYPOINT_CALLABLE}:
        raise ValueError("candidate entry point is not the fixed synthetic entry point")
    modules = value["read_set_maximum"]
    if (not isinstance(modules, list) or tuple(modules) != READ_SET_HARD_MAXIMUM or
            any(module not in MODULE_IDS for module in modules)):
        raise ValueError("candidate read-set maximum mismatch")
    budget = value["budget_maximum"]
    if not isinstance(budget, dict) or set(budget) != set(BudgetLimits().__dict__):
        raise ValueError("candidate budget maximum schema invalid")
    try:
        validate_limits(BudgetLimits(**budget))
    except Exception as exc:
        raise ValueError("candidate budget maximum exceeds hard-coded limits") from exc
    if budget != BudgetLimits().__dict__:
        raise ValueError("candidate budget maximum differs from code-fixed maximum")
    return value
