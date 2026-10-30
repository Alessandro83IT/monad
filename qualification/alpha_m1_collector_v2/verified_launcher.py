"""Non-operational, synthetic-only verified launch-chain prototype.

This Python module cannot establish trust in the interpreter or in its own
bootstrap. It therefore refuses every operational payload and executes only
the fixed, import-free synthetic entry point from bytes retained in memory.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import hmac
import os
import re
import stat
import time
from typing import Any, Mapping

from .candidate_identity import (
    AUTHORIZED_COMPONENT_PATHS, ENTRYPOINT_PATH, aggregate_digest,
    validate_identity_manifest,
)
from .contracts import InvalidRequest
from .nonce_ledger import NonceLedger
from .target_attestation import VerifiedTarget, verify_target_attestation
from .trust_chain import POLICY_VERSION, TrustStore, canonical_json, load_trust_store, strict_json

MAX_MANIFEST_BYTES = 128 * 1024
MAX_APPROVAL_TTL = 300
HEX = re.compile(r"[0-9a-f]{64}\Z")
TOKEN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._:-]{0,95}\Z")


@dataclass(frozen=True)
class Approval:
    kind: str
    key_id: str
    approver_id: str
    issued_at: int
    expires_at: int
    nonce: str
    candidate_sha256: str
    target_id: str
    modules: tuple[str, ...]
    budgets: Mapping[str, int]
    signature: str

    def payload(self) -> dict[str, Any]:
        return {
            "schema_version": 1, "policy_version": POLICY_VERSION,
            "kind": self.kind, "key_id": self.key_id,
            "approver_id": self.approver_id, "issued_at": self.issued_at,
            "expires_at": self.expires_at, "nonce": self.nonce,
            "candidate_sha256": self.candidate_sha256, "target_id": self.target_id,
            "modules": list(self.modules), "budgets": dict(self.budgets),
        }


def _read_component(root_fd: int, path: str, *, maximum: int) -> bytes:
    parts = path.split("/")
    if not parts or any(part in {"", ".", ".."} for part in parts):
        raise InvalidRequest("candidate path is not canonical")
    current = os.dup(root_fd)
    try:
        for part in parts[:-1]:
            next_fd = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                              dir_fd=current)
            os.close(current)
            current = next_fd
        fd = os.open(parts[-1], os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK,
                     dir_fd=current)
        try:
            before = os.fstat(fd)
            if (not stat.S_ISREG(before.st_mode) or before.st_nlink != 1 or
                    before.st_size <= 0 or before.st_size > maximum or
                    before.st_uid != os.geteuid()):
                raise InvalidRequest("candidate component type, owner, link count, or size invalid")
            data = bytearray()
            while len(data) < before.st_size:
                block = os.read(fd, min(65536, before.st_size - len(data)))
                if not block:
                    raise InvalidRequest("candidate component shortened during read")
                data.extend(block)
            after = os.fstat(fd)
            identity = lambda x: (x.st_dev, x.st_ino, x.st_mode, x.st_nlink,
                                  x.st_size, x.st_mtime_ns, x.st_ctime_ns)
            if identity(before) != identity(after):
                raise InvalidRequest("candidate component changed during read")
            return bytes(data)
        finally:
            os.close(fd)
    finally:
        os.close(current)


def _verify_approval(approval: Approval, *, trust: TrustStore, role: str,
                     expected_kind: str, candidate_digest: str, target_id: str,
                     now: int, expected_modules: tuple[str, ...] | None = None,
                     expected_nonce: str | None = None) -> None:
    if not isinstance(approval, Approval) or approval.kind != expected_kind:
        raise InvalidRequest("required signed approval is absent or has wrong kind")
    if (not TOKEN.fullmatch(approval.approver_id) or approval.candidate_sha256 != candidate_digest or
            approval.target_id != target_id or approval.issued_at > now or
            approval.expires_at <= now or approval.expires_at <= approval.issued_at or
            approval.expires_at - approval.issued_at > MAX_APPROVAL_TTL):
        raise InvalidRequest("approval identity, target, digest, or freshness is invalid")
    if expected_modules is not None and approval.modules != expected_modules:
        raise InvalidRequest("approval module scope does not exactly match requested scope")
    if expected_nonce is not None and approval.nonce != expected_nonce:
        raise InvalidRequest("separate approval nonces do not match the launch authorization")
    trust.verify(role, approval.key_id, approval.payload(), approval.signature,
                 expected_principal=approval.approver_id)


def _verify_manifest_components(root_fd: int, manifest_raw: bytes, *, trust: TrustStore,
                                candidate_approval: Approval, target_id: str,
                                now: int) -> tuple[dict[str, Any], dict[str, bytes]]:
    manifest = validate_identity_manifest(strict_json(manifest_raw, maximum=MAX_MANIFEST_BYTES))
    manifest_digest = hashlib.sha256(manifest_raw).hexdigest()
    _verify_approval(candidate_approval, trust=trust, role="candidate-approver",
                     expected_kind="candidate-identity", candidate_digest=manifest_digest,
                     target_id=target_id, now=now)
    captured: dict[str, bytes] = {}
    rows = []
    row_by_path = {row["path"]: row for row in manifest["components"]}
    for path in AUTHORIZED_COMPONENT_PATHS:
        raw = _read_component(root_fd, path, maximum=1024 * 1024)
        row = row_by_path[path]
        observed = hashlib.sha256(raw).hexdigest()
        if len(raw) != row["bytes"] or observed != row["sha256"]:
            raise InvalidRequest("candidate component differs from authenticated manifest")
        captured[path] = raw
        rows.append({"path": path, "bytes": len(raw), "sha256": observed})
    if aggregate_digest(rows) != manifest["aggregate_sha256"]:
        raise InvalidRequest("measured candidate aggregate digest mismatch")
    return manifest, captured


def launch_synthetic(*, candidate_root_fd: int, trust_directory_fd: int,
                     ledger_directory_fd: int, root_anchor: bytes,
                     manifest_raw: bytes, candidate_approval: Approval,
                     target_attestation: VerifiedTarget, readset_approval: Approval,
                     execution_approval: Approval, expected_target_id: str,
                     operational: bool = False) -> dict[str, Any]:
    """Verify a complete synthetic authorization chain and run only a safe stub.

    `operational=True` always fails closed. No real collector backend exists in
    this version, and the nonce is not consumed for a refused operational run.
    """
    if operational:
        raise InvalidRequest("operational launch is disabled in this candidate")
    # Never accept caller-supplied time as evidence of freshness. The wall
    # clock itself remains an environmental trust dependency (see candidate
    # report); a production launcher needs a separately qualified clock.
    timestamp = int(time.time())
    trust = load_trust_store(trust_directory_fd, root_anchor=root_anchor)
    target_summary = verify_target_attestation(target_attestation, trust_store=trust,
                                               expected_target_id=expected_target_id,
                                               now=timestamp)
    manifest, captured = _verify_manifest_components(
        candidate_root_fd, manifest_raw, trust=trust,
        candidate_approval=candidate_approval, target_id=expected_target_id, now=timestamp)
    modules = tuple(manifest["read_set_maximum"])
    budgets = manifest["budget_maximum"]
    digest = hashlib.sha256(manifest_raw).hexdigest()
    _verify_approval(readset_approval, trust=trust, role="read-set-approver",
                     expected_kind="read-set", candidate_digest=digest,
                     target_id=expected_target_id, now=timestamp,
                     expected_modules=modules)
    _verify_approval(execution_approval, trust=trust, role="execution-approver",
                     expected_kind="execution", candidate_digest=digest,
                     target_id=expected_target_id, now=timestamp,
                     expected_modules=modules, expected_nonce=readset_approval.nonce)
    if (not _valid_approved_budgets(readset_approval.budgets) or
            not _valid_approved_budgets(execution_approval.budgets) or
            dict(readset_approval.budgets) != budgets or dict(execution_approval.budgets) != budgets or
            readset_approval.approver_id == execution_approval.approver_id or
            readset_approval.key_id == execution_approval.key_id):
        raise InvalidRequest("approval budgets mismatch or approver roles are not separated")
    nonce = readset_approval.nonce
    if not isinstance(nonce, str) or not re.fullmatch(r"[A-Za-z0-9_-]{22,96}", nonce):
        raise InvalidRequest("launch nonce is malformed")
    entry_raw = captured[ENTRYPOINT_PATH]
    code = compile(entry_raw, "<verified-synthetic-entry>", "exec", dont_inherit=True)
    result: dict[str, Any] = {"target": target_summary, "candidate_sha256": digest,
                              "completion": "INCONCLUSIVE", "entry_result": None}
    with NonceLedger(ledger_directory_fd, root_anchor=root_anchor) as ledger:
        ledger.consume(nonce, secrets_token(digest, nonce))
        try:
            namespace: dict[str, Any] = {"__builtins__": {}, "__name__": "verified_synthetic_entry"}
            exec(code, namespace, namespace)
            entry = namespace.get("main")
            if not callable(entry):
                raise InvalidRequest("verified synthetic entry point is not callable")
            value = entry()
            if value != {"status": "SYNTHETIC_ONLY", "collector_started": False}:
                raise InvalidRequest("synthetic entry point returned unexpected output")
            result["entry_result"] = value
            result["completion"] = "SYNTHETIC_ONLY"
            ledger.finish(nonce, success=True)
        except BaseException:
            try:
                ledger.finish(nonce, success=False)
            finally:
                raise
    return result


def _valid_approved_budgets(value: Mapping[str, Any]) -> bool:
    return (isinstance(value, Mapping) and set(value) == set(manifest_budget_keys()) and
            all(type(item) is int for item in value.values()))


def manifest_budget_keys() -> tuple[str, ...]:
    # Keep the wire schema tied to the same hard-coded contract as the identity.
    from .contracts import BudgetLimits
    return tuple(BudgetLimits().__dict__)


def secrets_token(candidate_digest: str, nonce: str) -> str:
    """Stable opaque record reference; it is not an authorization secret."""
    return hashlib.sha256((candidate_digest + ":" + nonce).encode("ascii")).hexdigest()
