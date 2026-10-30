"""Versioned role trust store and canonical signature verification.

The verifier never creates a trust store or signs approvals. The root anchor
is injected by a separately configured launcher; tests use ephemeral keys.
"""
from __future__ import annotations

import hashlib
import hmac
import json
import os
import re
import stat
from typing import Any, Mapping

from .contracts import InvalidRequest

POLICY_VERSION = "alpha-m1-trust-policy-v2"
TRUST_STORE_SCHEMA_VERSION = 1
TRUST_STORE_FILENAME = "trust-store.json"
MAX_TRUST_STORE_BYTES = 128 * 1024
ROLE_IDS = (
    "candidate-approver", "read-set-approver", "execution-approver",
    "operator-target-attestor",
)
HEX_256 = re.compile(r"[0-9a-f]{64}\Z")
KEY_ID = re.compile(r"[A-Za-z0-9][A-Za-z0-9._:-]{0,63}\Z")
PRINCIPAL_ID = re.compile(r"[A-Za-z0-9][A-Za-z0-9._:@-]{0,95}\Z")


def canonical_json(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("ascii")


def canonical_document(value: Any) -> bytes:
    return canonical_json(value) + b"\n"


def strict_json(raw: bytes, *, maximum: int) -> Any:
    if not isinstance(raw, bytes) or len(raw) > maximum:
        raise InvalidRequest("signed JSON document exceeds fixed byte limit")

    def pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in items:
            if key in result:
                raise InvalidRequest("duplicate JSON object key")
            result[key] = value
        return result

    try:
        return json.loads(raw.decode("ascii"), object_pairs_hook=pairs)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise InvalidRequest("signed JSON document is not strict ASCII JSON") from exc


def read_regular_at(directory_fd: int, name: str, *, maximum: int,
                    require_private: bool) -> bytes:
    if (not isinstance(name, str) or not name or name in {".", ".."} or
            "/" in name or "\x00" in name):
        raise InvalidRequest("trust input must be one fixed path component")
    flags = os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK
    fd = os.open(name, flags, dir_fd=directory_fd)
    try:
        before = os.fstat(fd)
        if (not stat.S_ISREG(before.st_mode) or before.st_nlink != 1 or
                before.st_uid != os.geteuid() or before.st_size > maximum or
                (require_private and before.st_mode & 0o077)):
            raise InvalidRequest("trust input has unsafe type, owner, link count, mode, or size")
        chunks: list[bytes] = []
        remaining = before.st_size
        while remaining:
            block = os.read(fd, min(64 * 1024, remaining))
            if not block:
                raise InvalidRequest("trust input ended before its declared size")
            chunks.append(block)
            remaining -= len(block)
        after = os.fstat(fd)
        before_identity = (before.st_dev, before.st_ino, before.st_mode, before.st_nlink,
                           before.st_size, before.st_mtime_ns, before.st_ctime_ns)
        after_identity = (after.st_dev, after.st_ino, after.st_mode, after.st_nlink,
                          after.st_size, after.st_mtime_ns, after.st_ctime_ns)
        if before_identity != after_identity:
            raise InvalidRequest("trust input changed during read")
        return b"".join(chunks)
    finally:
        os.close(fd)


class TrustStore:
    def __init__(self, payload: Mapping[str, Any], *, root_anchor: bytes):
        if not isinstance(root_anchor, bytes) or len(root_anchor) < 32:
            raise InvalidRequest("independent root trust anchor is missing")
        if not isinstance(payload, Mapping) or set(payload) != {
            "schema_version", "policy_version", "roles", "root_signature",
        }:
            raise InvalidRequest("trust store envelope schema invalid")
        unsigned = {key: payload[key] for key in ("schema_version", "policy_version", "roles")}
        signature = payload["root_signature"]
        if (payload["schema_version"] != TRUST_STORE_SCHEMA_VERSION or
                payload["policy_version"] != POLICY_VERSION or
                not isinstance(signature, str) or not HEX_256.fullmatch(signature)):
            raise InvalidRequest("trust store version or root signature invalid")
        expected = hmac.new(root_anchor, canonical_json(unsigned), hashlib.sha256).hexdigest()
        if not hmac.compare_digest(expected, signature):
            raise InvalidRequest("trust store is not authenticated by the configured root")
        roles = payload["roles"]
        if not isinstance(roles, Mapping) or set(roles) != set(ROLE_IDS):
            raise InvalidRequest("trust store role set is incomplete or expanded")
        keys: dict[str, dict[str, tuple[bytes, str]]] = {}
        all_material: set[bytes] = set()
        all_ids: set[str] = set()
        for role in ROLE_IDS:
            entries = roles[role]
            if not isinstance(entries, list) or not entries:
                raise InvalidRequest("every trust role needs at least one key")
            role_keys: dict[str, tuple[bytes, str]] = {}
            for item in entries:
                if not isinstance(item, Mapping) or set(item) != {"key_id", "key_hex", "principal_id"}:
                    raise InvalidRequest("trust key entry schema invalid")
                key_id, key_hex, principal_id = item["key_id"], item["key_hex"], item["principal_id"]
                if (not isinstance(key_id, str) or not KEY_ID.fullmatch(key_id) or
                        key_id in all_ids or not isinstance(key_hex, str) or
                        not re.fullmatch(r"[0-9a-f]{64,128}", key_hex) or len(key_hex) % 2 or
                        not isinstance(principal_id, str) or not PRINCIPAL_ID.fullmatch(principal_id)):
                    raise InvalidRequest("trust key identifier or encoding invalid")
                key = bytes.fromhex(key_hex)
                if key in all_material or hmac.compare_digest(key, root_anchor):
                    raise InvalidRequest("role trust keys must use distinct key material")
                role_keys[key_id] = (key, principal_id)
                all_ids.add(key_id)
                all_material.add(key)
            keys[role] = role_keys
        self.policy_version = POLICY_VERSION
        self.__keys = keys

    @classmethod
    def open_preconfigured(cls, directory_fd: int, *, root_anchor: bytes) -> "TrustStore":
        raw = read_regular_at(directory_fd, TRUST_STORE_FILENAME,
                             maximum=MAX_TRUST_STORE_BYTES, require_private=True)
        payload = strict_json(raw, maximum=MAX_TRUST_STORE_BYTES)
        return cls(payload, root_anchor=root_anchor)

    def verify(self, role: str, key_id: str, payload: Mapping[str, Any], signature: str,
               *, expected_principal: str) -> None:
        if role not in self.__keys:
            raise InvalidRequest("signature role is not trusted")
        trusted = self.__keys[role].get(key_id) if isinstance(key_id, str) else None
        if (trusted is None or trusted[1] != expected_principal or
                not isinstance(signature, str) or not HEX_256.fullmatch(signature)):
            raise InvalidRequest("signature key is not trusted for this role")
        key = trusted[0]
        expected = hmac.new(key, canonical_json(payload), hashlib.sha256).hexdigest()
        if not hmac.compare_digest(expected, signature):
            raise InvalidRequest("role signature verification failed")


def load_trust_store(directory_fd: int, *, root_anchor: bytes) -> TrustStore:
    """Fail-closed trust-store open; it never provisions or repairs key data."""
    return TrustStore.open_preconfigured(directory_fd, root_anchor=root_anchor)
