"""Strict AAP-v1 wire codec and Ed25519 verification for synthetic contracts.

This module does not collect diagnostics or create signing keys. Private-key
signing is intentionally left to an external approval system/test fixture.
"""
from __future__ import annotations

import ctypes
import ctypes.util
import hashlib
import json
import re
import time
from typing import Any, Mapping

PROTOCOL = "AAP-v1"
ALGORITHM = "Ed25519"
MAX_MESSAGE_BYTES = 128 * 1024
MAX_INT = (1 << 63) - 1
MIN_INT = -(1 << 63)
MAX_TIMESTAMP = 4_102_444_800  # 2100-01-01 UTC, exclusive policy ceiling
MAX_TTL_SECONDS = 300
HARD_MODULES = ["identity", "systemd-state", "process-cgroups"]
HARD_BUDGETS = {
    "bytes_read": 603979776, "bytes_persisted": 4194304, "files": 256,
    "records": 8192, "processes": 4096, "units": 32,
    "duration_seconds": 180, "bytes_per_file": 134217728,
    "bytes_per_source": 268435456, "output_file_bytes": 65536,
    "manifest_bytes": 65536, "seal_bytes": 4096,
}
SIGNATURE_DOMAIN = b"MONAD-AAP-v1\0"
KINDS = frozenset({"trust-store", "candidate-identity", "read-set", "execution", "target-attestation"})


class AAPError(ValueError):
    """Malformed, non-canonical, unauthenticated, or out-of-policy AAP input."""


def _validate_value(value: Any, depth: int = 0) -> None:
    if depth > 32:
        raise AAPError("AAP JSON nesting exceeds limit")
    if value is None or type(value) is bool:
        return
    if type(value) is int:
        if not MIN_INT <= value <= MAX_INT:
            raise AAPError("AAP integer is outside signed 64-bit range")
        return
    if type(value) is str:
        if any(ord(char) < 0x20 or ord(char) > 0x7e for char in value):
            raise AAPError("AAP strings must be printable ASCII; Unicode normalization is forbidden")
        return
    if type(value) is list:
        if len(value) > 8192:
            raise AAPError("AAP array exceeds item limit")
        for item in value:
            _validate_value(item, depth + 1)
        return
    if type(value) is dict:
        if len(value) > 8192:
            raise AAPError("AAP object exceeds member limit")
        for key, item in value.items():
            if type(key) is not str:
                raise AAPError("AAP object keys must be strings")
            _validate_value(key, depth + 1)
            _validate_value(item, depth + 1)
        return
    raise AAPError("AAP value type is not permitted")


def canonical_bytes(value: Any) -> bytes:
    """Encode the exact AAP-v1 ASCII JSON representation (no trailing LF)."""
    _validate_value(value)
    try:
        return json.dumps(value, sort_keys=True, separators=(",", ":"),
                          ensure_ascii=False, allow_nan=False).encode("ascii")
    except (TypeError, ValueError, UnicodeEncodeError) as exc:
        raise AAPError("AAP value cannot be canonically encoded") from exc


def _pairs_no_duplicates(items: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in items:
        if key in result:
            raise AAPError("duplicate AAP object member")
        result[key] = value
    return result


def _parse_int(raw: str) -> int:
    value = int(raw)
    if not MIN_INT <= value <= MAX_INT:
        raise AAPError("AAP integer is outside signed 64-bit range")
    return value


def _reject_float(_: str) -> Any:
    raise AAPError("AAP does not permit floating-point values")


def parse_canonical(raw: bytes, *, maximum_bytes: int = MAX_MESSAGE_BYTES) -> dict[str, Any]:
    if type(maximum_bytes) is not int or maximum_bytes <= 0 or type(raw) is not bytes or not raw or len(raw) > maximum_bytes:
        raise AAPError("AAP message is empty or exceeds byte limit")
    try:
        text = raw.decode("ascii", errors="strict")
        value = json.loads(text, object_pairs_hook=_pairs_no_duplicates,
                           parse_int=_parse_int, parse_float=_reject_float,
                           parse_constant=_reject_float)
    except AAPError:
        raise
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
        raise AAPError("AAP message is not valid strict ASCII JSON") from exc
    if type(value) is not dict:
        raise AAPError("AAP message top level must be an object")
    _validate_value(value)
    if canonical_bytes(value) != raw:
        raise AAPError("AAP message bytes are not canonical")
    return value


def signing_bytes(kind: str, unsigned_message: Mapping[str, Any]) -> bytes:
    if type(kind) is not str or kind not in KINDS:
        raise AAPError("unknown AAP message kind")
    if type(unsigned_message) is not dict or "signature" in unsigned_message:
        raise AAPError("signing input must be an unsigned AAP object")
    return SIGNATURE_DOMAIN + kind.encode("ascii") + b"\0" + canonical_bytes(dict(unsigned_message))


def message_digest(raw: bytes) -> str:
    parse_canonical(raw)
    return hashlib.sha256(raw).hexdigest()


def authorization_digest(messages: Mapping[str, bytes]) -> str:
    names = {
        "candidate-approval.json": "candidate",
        "readset-approval.json": "readset",
        "execution-approval.json": "execution",
        "target-attestation.json": "target",
    }
    if set(messages) != set(names):
        raise AAPError("authorization digest requires the exact four AAP documents")
    signatures: dict[str, str] = {}
    for filename, label in names.items():
        value = parse_canonical(messages[filename])
        signature = value.get("signature")
        if type(signature) is not str or len(signature) != 128 or any(c not in "0123456789abcdef" for c in signature):
            raise AAPError("authorization document signature encoding is invalid")
        signatures[label] = signature
    return hashlib.sha256(canonical_bytes(signatures)).hexdigest()


def parse_reservation_ledger(raw: bytes) -> dict[str, Any]:
    ledger = parse_canonical(raw, maximum_bytes=4 * 1024 * 1024)
    if set(ledger) != {"schema_version", "protocol_version", "ledger_id", "records", "sha256"} or \
            ledger["schema_version"] != 2 or ledger["protocol_version"] != PROTOCOL or \
            ledger["ledger_id"] != "alpha-m1-synthetic-ledger-v1":
        raise AAPError("reservation ledger identity or schema mismatch")
    checksum = ledger["sha256"]
    unsigned = dict(ledger); del unsigned["sha256"]
    if type(checksum) is not str or checksum != hashlib.sha256(canonical_bytes(unsigned)).hexdigest():
        raise AAPError("reservation ledger checksum mismatch")
    records = ledger["records"]
    if type(records) is not list or len(records) > 4096:
        raise AAPError("reservation ledger record count invalid")
    seen_nonce: set[str] = set()
    seen_reservation: set[str] = set()
    expected = {"authorization_sha256", "candidate_sha256", "ledger_id", "nonce",
                "protocol_version", "reservation_id", "state", "target_id"}
    for row in records:
        if type(row) is not dict or set(row) != expected or row.get("protocol_version") != PROTOCOL or \
                row.get("ledger_id") != "alpha-m1-synthetic-ledger-v1" or row.get("state") not in {"RESERVED", "CONSUMED"}:
            raise AAPError("reservation ledger record schema or state invalid")
        nonce, reservation = row.get("nonce"), row.get("reservation_id")
        if (type(nonce) is not str or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._:-]{21,95}", nonce) or
                type(reservation) is not str or not re.fullmatch(r"res-[0-9a-f]{32}", reservation) or
                type(row.get("target_id")) is not str or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._:-]{0,95}", row["target_id"]) or
                any(type(row.get(field)) is not str or not re.fullmatch(r"[0-9a-f]{64}", row[field])
                    for field in ("candidate_sha256", "authorization_sha256")) or
                nonce in seen_nonce or reservation in seen_reservation):
            raise AAPError("reservation ledger binding is invalid or duplicated")
        seen_nonce.add(nonce); seen_reservation.add(reservation)
    return ledger


def ledger_authorization_digest(messages: Mapping[str, bytes]) -> str:
    """Digest the four signature values in the same fixed labels as native."""
    names = {"candidate-approval.json": "candidate", "readset-approval.json": "readset",
             "execution-approval.json": "execution", "target-attestation.json": "target"}
    if set(messages) != set(names):
        raise AAPError("ledger binding requires exactly four authorization documents")
    signatures = {}
    for filename, label in names.items():
        value = parse_canonical(messages[filename])
        signatures[label] = value["signature"]
    return hashlib.sha256(canonical_bytes(signatures)).hexdigest()


def _libcrypto():
    path = ctypes.util.find_library("crypto")
    if not path:
        raise AAPError("OpenSSL libcrypto is unavailable")
    lib = ctypes.CDLL(path)
    lib.EVP_PKEY_new_raw_public_key.argtypes = [ctypes.c_int, ctypes.c_void_p,
                                                ctypes.c_void_p, ctypes.c_size_t]
    lib.EVP_PKEY_new_raw_public_key.restype = ctypes.c_void_p
    lib.EVP_PKEY_free.argtypes = [ctypes.c_void_p]
    lib.EVP_MD_CTX_new.restype = ctypes.c_void_p
    lib.EVP_MD_CTX_free.argtypes = [ctypes.c_void_p]
    lib.EVP_DigestVerifyInit.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                         ctypes.c_void_p, ctypes.c_void_p,
                                         ctypes.c_void_p]
    lib.EVP_DigestVerifyInit.restype = ctypes.c_int
    lib.EVP_DigestVerify.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                     ctypes.c_size_t, ctypes.c_void_p,
                                     ctypes.c_size_t]
    lib.EVP_DigestVerify.restype = ctypes.c_int
    return lib


def verify_ed25519(public_key: bytes, kind: str, unsigned_message: Mapping[str, Any],
                   signature: bytes) -> bool:
    if type(public_key) is not bytes or len(public_key) != 32 or type(signature) is not bytes or len(signature) != 64:
        return False
    signed = signing_bytes(kind, unsigned_message)
    key_buffer = ctypes.create_string_buffer(public_key)
    message_buffer = ctypes.create_string_buffer(signed)
    signature_buffer = ctypes.create_string_buffer(signature)
    lib = _libcrypto()
    # OpenSSL's stable EVP_PKEY_ED25519 NID is 1087.
    key = lib.EVP_PKEY_new_raw_public_key(1087, None, key_buffer, len(public_key))
    if not key:
        raise AAPError("OpenSSL could not construct Ed25519 public key")
    context = lib.EVP_MD_CTX_new()
    if not context:
        lib.EVP_PKEY_free(key)
        raise AAPError("OpenSSL could not allocate signature context")
    try:
        if lib.EVP_DigestVerifyInit(context, None, None, None, key) != 1:
            raise AAPError("OpenSSL could not initialize Ed25519 verification")
        return lib.EVP_DigestVerify(context, signature_buffer, len(signature),
                                    message_buffer, len(signed)) == 1
    finally:
        lib.EVP_MD_CTX_free(context)
        lib.EVP_PKEY_free(key)


def verify_envelope(raw: bytes, *, expected_kind: str, public_key: bytes,
                    exact_fields: set[str], now: int | None = None) -> dict[str, Any]:
    value = parse_canonical(raw)
    if set(value) != exact_fields or type(value.get("schema_version")) is not int or value.get("schema_version") != 1 or value.get("protocol_version") != PROTOCOL or \
            value.get("algorithm") != ALGORITHM or value.get("kind") != expected_kind:
        raise AAPError("AAP envelope schema, version, algorithm, or kind mismatch")
    signature_hex = value.get("signature")
    if type(signature_hex) is not str or len(signature_hex) != 128 or any(c not in "0123456789abcdef" for c in signature_hex):
        raise AAPError("AAP signature encoding is invalid")
    unsigned = dict(value)
    del unsigned["signature"]
    if not verify_ed25519(public_key, expected_kind, unsigned, bytes.fromhex(signature_hex)):
        raise AAPError("AAP Ed25519 signature verification failed")
    if "issued_at" in value or "expires_at" in value:
        issued, expires = value.get("issued_at"), value.get("expires_at")
        if (type(issued) is not int or type(expires) is not int or
                not 0 <= issued < expires <= MAX_TIMESTAMP or expires - issued > MAX_TTL_SECONDS):
            raise AAPError("AAP validity interval is outside policy")
        if now is not None and (type(now) is not int or issued > now or expires <= now):
            raise AAPError("AAP authorization is not currently valid")
    return value


def verify_trust_store(raw: bytes, *, root_public_key: bytes,
                       now: int) -> dict[str, dict[str, Any]]:
    if type(now) is not int or not 0 <= now < MAX_TIMESTAMP:
        raise AAPError("AAP clock value is outside policy")
    fields = {"algorithm", "keys", "kind", "protocol_version", "schema_version", "signature"}
    envelope = verify_envelope(raw, expected_kind="trust-store", public_key=root_public_key,
                               exact_fields=fields, now=None)
    rows = envelope["keys"]
    roles = {"candidate-approver", "read-set-approver", "execution-approver",
             "operator-target-attestor"}
    if type(rows) is not list or len(rows) != len(roles):
        raise AAPError("AAP trust store must contain exactly one key per role")
    result: dict[str, dict[str, Any]] = {}
    seen_roles: set[str] = set()
    seen_public: set[str] = set()
    seen_principals: set[str] = set()
    token = re.compile(r"[A-Za-z0-9][A-Za-z0-9._:-]{0,95}\Z")
    for row in rows:
        if type(row) is not dict or set(row) != {
            "key_id", "principal_id", "role", "public_key_hex", "not_before", "not_after",
        }:
            raise AAPError("AAP trust key schema is invalid")
        key_id, principal, role, public = (row[k] for k in
                                            ("key_id", "principal_id", "role", "public_key_hex"))
        before, after = row["not_before"], row["not_after"]
        if (type(key_id) is not str or len(key_id) > 64 or not token.fullmatch(key_id) or
                type(principal) is not str or not token.fullmatch(principal) or type(role) is not str or
                role not in roles or type(public) is not str or len(public) != 64 or
                any(c not in "0123456789abcdef" for c in public) or
                type(before) is not int or type(after) is not int or
                not 0 <= before < after <= MAX_TIMESTAMP or not before <= now < after):
            raise AAPError("AAP trust key fields or validity are invalid")
        if key_id in result or role in seen_roles or public in seen_public or principal in seen_principals:
            raise AAPError("AAP trust identities, roles, and keys must be distinct")
        result[key_id] = row
        seen_roles.add(role); seen_public.add(public); seen_principals.add(principal)
    if seen_roles != roles:
        raise AAPError("AAP trust store role set is incomplete")
    return result


def verify_authorization_bundle(*, trust_raw: bytes, root_public_key: bytes,
                                messages: Mapping[str, bytes], candidate_sha256: str,
                                target_id: str, modules: list[str], budgets: Mapping[str, int]) -> dict[str, Any]:
    """Verify the exact four-role AAP bundle against a fixed request contract."""
    import re
    def exact_budget(value: Any) -> bool:
        return (type(value) is dict and set(value) == set(HARD_BUDGETS) and
                all(type(value[name]) is int and value[name] == HARD_BUDGETS[name]
                    for name in HARD_BUDGETS))
    if not re.fullmatch(r"[0-9a-f]{64}", candidate_sha256):
        raise AAPError("candidate digest must be lowercase SHA-256")
    if (type(modules) is not list or modules != HARD_MODULES or not exact_budget(dict(budgets)) or
            type(target_id) is not str or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._:-]{0,95}", target_id)):
        raise AAPError("AAP request scope differs from fixed module or budget policy")
    now = int(time.time())
    trust = verify_trust_store(trust_raw, root_public_key=root_public_key, now=now)
    expected_kinds = {
        "candidate-identity": ("candidate-approval.json", "candidate-approver"),
        "read-set": ("readset-approval.json", "read-set-approver"),
        "execution": ("execution-approval.json", "execution-approver"),
        "target-attestation": ("target-attestation.json", "operator-target-attestor"),
    }
    if set(messages) != {filename for filename, _ in expected_kinds.values()}:
        raise AAPError("AAP authorization bundle is incomplete or expanded")
    approval_fields = {"algorithm", "schema_version", "protocol_version", "kind", "key_id",
                       "approver_id", "issued_at", "expires_at", "nonce", "candidate_sha256",
                       "target_id", "modules", "budgets", "signature"}
    target_fields = {"algorithm", "schema_version", "protocol_version", "kind", "key_id",
                     "operator_id", "candidate_sha256", "request_nonce", "target_id",
                     "declared_identity", "observed_identity", "provenance", "evidence_ref",
                     "issued_at", "expires_at", "snapshot_ref", "signature"}
    verified: dict[str, dict[str, Any]] = {}
    for kind, (filename, role) in expected_kinds.items():
        preliminary = parse_canonical(messages[filename])
        key_id = preliminary.get("key_id")
        key = trust.get(key_id) if type(key_id) is str else None
        if key is None or key["role"] != role:
            raise AAPError("AAP key is not trusted for required role")
        value = verify_envelope(messages[filename], expected_kind=kind,
                                public_key=bytes.fromhex(key["public_key_hex"]),
                                exact_fields=(target_fields if kind == "target-attestation" else approval_fields),
                                now=now)
        principal_field = "operator_id" if kind == "target-attestation" else "approver_id"
        if value.get(principal_field) != key["principal_id"]:
            raise AAPError("AAP signer principal does not match trust store")
        verified[kind] = value
    candidate = verified["candidate-identity"]
    readset = verified["read-set"]
    execution = verified["execution"]
    target = verified["target-attestation"]
    if (candidate["candidate_sha256"] != candidate_sha256 or
            readset["candidate_sha256"] != candidate_sha256 or
            execution["candidate_sha256"] != candidate_sha256 or
            target["candidate_sha256"] != candidate_sha256):
        raise AAPError("AAP candidate digest mismatch")
    if any(item.get("target_id") != target_id for item in (candidate, readset, execution, target)):
        raise AAPError("AAP target identity mismatch")
    nonce = readset.get("nonce")
    if type(nonce) is not str or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._:-]{21,95}", nonce):
        raise AAPError("AAP nonce format invalid")
    if candidate.get("nonce") != nonce or execution.get("nonce") != nonce or target.get("request_nonce") != nonce:
        raise AAPError("AAP nonce binding mismatch")
    if (candidate.get("modules") != [] or candidate.get("budgets") != {} or
            readset.get("modules") != modules or execution.get("modules") != modules or
            not exact_budget(readset.get("budgets")) or not exact_budget(execution.get("budgets"))):
        raise AAPError("AAP module or budget scope mismatch")
    principals = [candidate["approver_id"], readset["approver_id"],
                  execution["approver_id"], target["operator_id"]]
    if len(set(principals)) != 4:
        raise AAPError("AAP roles require distinct principals")
    declared, observed = target.get("declared_identity"), target.get("observed_identity")
    identity_fields = {"target_id", "machine_id", "hostname", "boot_id", "snapshot_id"}
    if (type(declared) is not dict or set(declared) != identity_fields or declared != observed or
            declared.get("target_id") != target_id or
            type(declared.get("machine_id")) is not str or not re.fullmatch(r"[0-9a-f]{32}", declared["machine_id"]) or
            type(declared.get("boot_id")) is not str or not re.fullmatch(
                r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}", declared["boot_id"]) or
            any(type(declared.get(name)) is not str or not re.fullmatch(
                r"[A-Za-z0-9][A-Za-z0-9._:-]{0,95}", declared[name])
                for name in ("target_id", "hostname", "snapshot_id")) or
            target.get("provenance") != "synthetic-provider-v1" or
            type(target.get("evidence_ref")) is not str or not re.fullmatch(
                r"[A-Za-z0-9][A-Za-z0-9._:-]{0,95}", target["evidence_ref"]) or
            target.get("snapshot_ref") != declared.get("snapshot_id")):
        raise AAPError("AAP synthetic target attestation mismatch")
    return {"candidate_sha256": candidate_sha256, "target_id": target_id,
            "nonce": nonce, "modules": list(modules), "budgets": dict(budgets),
            "decision": "VERIFIED"}
