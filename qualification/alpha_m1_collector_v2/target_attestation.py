"""Signed target-identity contract with a synthetic-only provider policy."""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import re
from typing import Any, Mapping
import uuid

from .contracts import InvalidRequest
from .trust_chain import POLICY_VERSION, TrustStore

ATTESTATION_SCHEMA_VERSION = 1
MAX_ATTESTATION_TTL_SECONDS = 300
OPAQUE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._:-]{0,95}\Z")
MACHINE_ID = re.compile(r"[0-9a-f]{32}\Z")
IDENTITY_FIELDS = frozenset({"target_id", "machine_id", "hostname", "boot_id", "snapshot_id"})


def _validate_identity(value: Any) -> dict[str, Any]:
    if not isinstance(value, Mapping) or set(value) != IDENTITY_FIELDS:
        raise InvalidRequest("target identity fields are missing or unapproved")
    if (not isinstance(value["target_id"], str) or not OPAQUE.fullmatch(value["target_id"]) or
            not isinstance(value["machine_id"], str) or not MACHINE_ID.fullmatch(value["machine_id"]) or
            not isinstance(value["hostname"], str) or not OPAQUE.fullmatch(value["hostname"]) or
            not isinstance(value["snapshot_id"], str) or not OPAQUE.fullmatch(value["snapshot_id"])):
        raise InvalidRequest("target identity value format invalid")
    try:
        if str(uuid.UUID(value["boot_id"])) != value["boot_id"]:
            raise ValueError
    except (ValueError, TypeError, AttributeError):
        raise InvalidRequest("target boot ID is absent or malformed") from None
    return {name: value[name] for name in sorted(IDENTITY_FIELDS)}


@dataclass(frozen=True)
class VerifiedTarget:
    declared_identity: Mapping[str, Any]
    observed_identity: Mapping[str, Any]
    provenance: str
    evidence_ref: str
    operator_id: str
    attested_at: int
    expires_at: int
    snapshot_ref: str
    attestor_key_id: str
    signature: str

    def unsigned_payload(self) -> dict[str, Any]:
        return {
            "schema_version": ATTESTATION_SCHEMA_VERSION,
            "policy_version": POLICY_VERSION,
            "kind": "target-attestation",
            "declared_identity": dict(self.declared_identity),
            "observed_identity": dict(self.observed_identity),
            "provenance": self.provenance,
            "evidence_ref": self.evidence_ref,
            "operator_id": self.operator_id,
            "attested_at": self.attested_at,
            "expires_at": self.expires_at,
            "snapshot_ref": self.snapshot_ref,
            "attestor_key_id": self.attestor_key_id,
        }


def verify_target_attestation(attestation: VerifiedTarget, *, trust_store: TrustStore,
                              expected_target_id: str, now: int) -> dict[str, Any]:
    if not isinstance(attestation, VerifiedTarget):
        raise InvalidRequest("signed target attestation is required")
    declared = _validate_identity(attestation.declared_identity)
    observed = _validate_identity(attestation.observed_identity)
    if declared != observed:
        raise InvalidRequest("declared and observed target identities do not match")
    if declared["target_id"] != expected_target_id:
        raise InvalidRequest("observed target does not match authorized target")
    if (attestation.provenance != "synthetic-provider-v1" or
            not isinstance(attestation.evidence_ref, str) or not OPAQUE.fullmatch(attestation.evidence_ref) or
            not isinstance(attestation.snapshot_ref, str) or
            attestation.snapshot_ref != observed["snapshot_id"] or
            not isinstance(attestation.operator_id, str) or not OPAQUE.fullmatch(attestation.operator_id)):
        raise InvalidRequest("target evidence provenance, snapshot, or operator is invalid")
    if (isinstance(attestation.attested_at, bool) or not isinstance(attestation.attested_at, int) or
            isinstance(attestation.expires_at, bool) or not isinstance(attestation.expires_at, int) or
            attestation.expires_at <= attestation.attested_at or
            attestation.expires_at - attestation.attested_at > MAX_ATTESTATION_TTL_SECONDS or
            attestation.attested_at > now or attestation.expires_at <= now):
        raise InvalidRequest("target attestation is expired or outside the freshness window")
    trust_store.verify("operator-target-attestor", attestation.attestor_key_id,
                       attestation.unsigned_payload(), attestation.signature,
                       expected_principal=attestation.operator_id)
    return {
        "target_id": observed["target_id"],
        "identity_sha256": hashlib.sha256(
            json.dumps(observed, sort_keys=True, separators=(",", ":")).encode("ascii")
        ).hexdigest(),
        "provenance": attestation.provenance,
        "evidence_ref": attestation.evidence_ref,
        "operator_id": attestation.operator_id,
        "snapshot_ref": attestation.snapshot_ref,
        "attestor_key_id": attestation.attestor_key_id,
        "attested_at": attestation.attested_at,
        "expires_at": attestation.expires_at,
    }
