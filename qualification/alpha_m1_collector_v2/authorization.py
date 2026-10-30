"""Two separately signed authorization decisions; no issuer or credentials here."""
from __future__ import annotations

from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import hashlib
import hmac
import json
import re
import threading
from typing import Any, Mapping
import uuid

from .contracts import BudgetLimits, InvalidRequest, MODULE_IDS, validate_limits

HEX_256 = re.compile(r"[0-9a-f]{64}\Z")
OPAQUE_ID = re.compile(r"[A-Za-z0-9][A-Za-z0-9._:-]{0,95}\Z")
MAX_APPROVAL_TTL_SECONDS = 3600


def _canonical(payload: Mapping[str, Any]) -> bytes:
    return json.dumps(payload, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("ascii")


@dataclass(frozen=True)
class ReadSetApproval:
    approval_id: str
    candidate_id: str
    candidate_digest: str
    requested_read_set: tuple[str, ...]
    authorized_modules: tuple[str, ...]
    authorized_budgets: Mapping[str, int]
    target_id: str
    decision: str
    decision_maker: str
    issued_at: int
    expires_at: int
    nonce: str
    key_id: str
    signature: str

    def unsigned_payload(self) -> dict[str, Any]:
        return {
            "kind": "read-set-approval", "approval_id": self.approval_id,
            "candidate_id": self.candidate_id, "candidate_digest": self.candidate_digest,
            "requested_read_set": list(self.requested_read_set),
            "authorized_modules": list(self.authorized_modules),
            "authorized_budgets": dict(self.authorized_budgets), "target_id": self.target_id,
            "decision": self.decision, "decision_maker": self.decision_maker,
            "issued_at": self.issued_at, "expires_at": self.expires_at,
            "nonce": self.nonce, "key_id": self.key_id,
        }


@dataclass(frozen=True)
class ExecutionApproval:
    approval_id: str
    read_set_approval_id: str
    candidate_id: str
    candidate_digest: str
    target_id: str
    decision: str
    decision_maker: str
    issued_at: int
    expires_at: int
    nonce: str
    key_id: str
    signature: str

    def unsigned_payload(self) -> dict[str, Any]:
        return {
            "kind": "execution-approval", "approval_id": self.approval_id,
            "read_set_approval_id": self.read_set_approval_id,
            "candidate_id": self.candidate_id, "candidate_digest": self.candidate_digest,
            "target_id": self.target_id, "decision": self.decision,
            "decision_maker": self.decision_maker, "issued_at": self.issued_at,
            "expires_at": self.expires_at, "nonce": self.nonce, "key_id": self.key_id,
        }


@dataclass(frozen=True)
class AuthorizationBundle:
    read_set: ReadSetApproval
    execution: ExecutionApproval | None


class AuthorizationVerifier:
    """Verifies separately signed approvals and consumes their one-use nonces.

    Keys must come from a separately controlled trust source. This module has
    no key generation, signing, or operational credential facility. Tests use
    ephemeral synthetic keys only. Nonce replay protection is in-memory here;
    operational use requires a durable approval service/replay ledger.
    """

    def __init__(self, trust_keys: Mapping[str, bytes], *, clock=lambda: int(datetime.now(timezone.utc).timestamp())):
        if not trust_keys or any(not isinstance(key, str) or not OPAQUE_ID.fullmatch(key) or
                                 not isinstance(value, bytes) or len(value) < 32
                                 for key, value in trust_keys.items()):
            raise InvalidRequest("authorization verifier requires externally managed 256-bit trust keys")
        self.__keys = dict(trust_keys)
        self._clock = clock
        self._used_nonces: set[str] = set()
        self._lock = threading.Lock()

    @staticmethod
    def _valid_uuid(value: str) -> bool:
        try:
            return str(uuid.UUID(value)) == value
        except (ValueError, TypeError, AttributeError):
            return False

    def _verify_signature(self, payload: Mapping[str, Any], signature: str, key_id: str) -> None:
        key = self.__keys.get(key_id) if isinstance(key_id, str) else None
        if key is None or not isinstance(signature, str) or not HEX_256.fullmatch(signature):
            raise InvalidRequest("authorization signature or key ID is invalid")
        try:
            expected = hmac.new(key, _canonical(payload), hashlib.sha256).hexdigest()
        except (TypeError, ValueError, UnicodeEncodeError) as exc:
            raise InvalidRequest("authorization payload is not canonicalizable") from exc
        if not hmac.compare_digest(expected, signature):
            raise InvalidRequest("authorization signature verification failed")

    def _check_times(self, issued_at: int, expires_at: int) -> None:
        if (isinstance(issued_at, bool) or not isinstance(issued_at, int) or
                isinstance(expires_at, bool) or not isinstance(expires_at, int) or
                expires_at <= issued_at or expires_at - issued_at > MAX_APPROVAL_TTL_SECONDS or
                issued_at > self._clock() or expires_at <= self._clock()):
            raise InvalidRequest("authorization is invalid, not yet valid, or expired")

    def verify_bundle(self, bundle: AuthorizationBundle, *, candidate_id: str,
                      candidate_digest: str, target_id: str,
                      requested_modules: tuple[str, ...], limits: BudgetLimits) -> dict[str, str]:
        if not isinstance(bundle, AuthorizationBundle) or not isinstance(bundle.read_set, ReadSetApproval):
            raise InvalidRequest("read-set approval is required")
        read = bundle.read_set
        execution = bundle.execution
        if not isinstance(execution, ExecutionApproval):
            raise InvalidRequest("separate execution approval is required")
        if (not isinstance(candidate_digest, str) or not HEX_256.fullmatch(candidate_digest) or
                not isinstance(target_id, str) or not isinstance(read.candidate_id, str) or
                not isinstance(read.candidate_digest, str) or not isinstance(read.target_id, str) or
                read.candidate_id != candidate_id or
                read.candidate_digest != candidate_digest or read.target_id != target_id or
                not OPAQUE_ID.fullmatch(target_id)):
            raise InvalidRequest("authorization candidate or target identity mismatch")
        if (not self._valid_uuid(read.approval_id) or not self._valid_uuid(read.nonce) or
                not self._valid_uuid(execution.approval_id) or not self._valid_uuid(execution.nonce) or
                not isinstance(read.decision_maker, str) or not isinstance(execution.decision_maker, str) or
                not isinstance(read.decision, str) or not isinstance(execution.decision, str) or
                not OPAQUE_ID.fullmatch(read.decision_maker) or
                not OPAQUE_ID.fullmatch(execution.decision_maker) or
                read.decision_maker == execution.decision_maker or
                read.key_id == execution.key_id or
                read.decision != "APPROVE" or execution.decision != "APPROVE"):
            raise InvalidRequest("separate human approvers and role keys are required")
        if (not isinstance(requested_modules, tuple) or not requested_modules or
                not isinstance(read.authorized_modules, tuple) or
                not isinstance(read.requested_read_set, tuple) or
                any(not isinstance(module, str) for module in (*requested_modules, *read.authorized_modules, *read.requested_read_set)) or
                len(set(requested_modules)) != len(requested_modules) or
                any(module not in MODULE_IDS for module in requested_modules) or
                len(read.authorized_modules) > len(MODULE_IDS) or
                len(read.requested_read_set) > len(MODULE_IDS) or
                len(set(read.authorized_modules)) != len(read.authorized_modules) or
                len(set(read.requested_read_set)) != len(read.requested_read_set) or
                any(module not in MODULE_IDS for module in (*read.authorized_modules, *read.requested_read_set))):
            raise InvalidRequest("authorization read set or module allowlist is malformed")
        if (set(read.requested_read_set) != set(requested_modules) or
                not set(requested_modules).issubset(read.authorized_modules)):
            raise InvalidRequest("requested modules exceed the signed read set")
        try:
            if (not isinstance(read.authorized_budgets, Mapping) or
                    set(read.authorized_budgets) != set(asdict(BudgetLimits()))):
                raise ValueError("approval must bind every budget field")
            approved_limits = BudgetLimits(**dict(read.authorized_budgets))
            validate_limits(approved_limits)
        except (TypeError, ValueError) as exc:
            raise InvalidRequest("authorized budget contract is malformed") from exc
        if any(getattr(limits, name) > getattr(approved_limits, name) for name in asdict(limits)):
            raise InvalidRequest("runtime budget exceeds signed authorization")
        self._check_times(read.issued_at, read.expires_at)
        if (execution.read_set_approval_id != read.approval_id or
                execution.candidate_id != candidate_id or execution.candidate_digest != candidate_digest or
                execution.target_id != target_id):
            raise InvalidRequest("execution approval does not bind this read-set approval and target")
        if read.nonce == execution.nonce:
            raise InvalidRequest("read-set and execution approvals require distinct nonces")
        self._check_times(execution.issued_at, execution.expires_at)
        self._verify_signature(read.unsigned_payload(), read.signature, read.key_id)
        self._verify_signature(execution.unsigned_payload(), execution.signature, execution.key_id)
        if hmac.compare_digest(self.__keys[read.key_id], self.__keys[execution.key_id]):
            raise InvalidRequest("read-set and execution approvals require distinct key material")
        with self._lock:
            nonces = {read.nonce, execution.nonce}
            if nonces & self._used_nonces:
                raise InvalidRequest("authorization nonce has already been consumed")
            self._used_nonces.update(nonces)
        return {
            "read_set_approval_id": read.approval_id,
            "execution_approval_id": execution.approval_id,
            "read_set_decision_maker": read.decision_maker,
            "execution_decision_maker": execution.decision_maker,
        }
