"""Strict wire-format validation for the Monad local verification broker."""

from __future__ import annotations

import json
import re
import uuid
from typing import Any

from . import MAX_PACKET_BYTES, MAX_RESPONSE_BYTES, PROTOCOL_VERSION

OPERATIONS = frozenset({
    "systemd_status", "failed_units", "artifact_metadata", "artifact_hashes",
    "fixture_mount", "fixture_inventory", "build_inventory", "environment_identity",
})
ERROR_CODES = frozenset({
    "PROTOCOL_ERROR", "AUTH_ERROR", "REPLAY", "INVALID_REQUEST", "OP_NOT_ALLOWED",
    "ACCESS_DENIED", "NOT_FOUND", "NOT_REGULAR_FILE", "NOT_DIRECTORY", "SYMLINK",
    "OUTPUT_TOO_LARGE", "TIMEOUT", "RATE_LIMIT", "ENVIRONMENT_MISMATCH", "UNAVAILABLE", "INTERNAL_ERROR",
})


class ProtocolError(ValueError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


def _pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ProtocolError("PROTOCOL_ERROR", "duplicate JSON key")
        result[key] = value
    return result


def decode_json(packet: bytes, *, response: bool = False) -> dict[str, Any]:
    limit = MAX_RESPONSE_BYTES if response else MAX_PACKET_BYTES
    if not isinstance(packet, bytes) or len(packet) > limit:
        raise ProtocolError("OUTPUT_TOO_LARGE", "packet exceeds protocol limit")
    try:
        value = json.loads(packet.decode("utf-8", "strict"), object_pairs_hook=_pairs,
                           parse_constant=lambda _: (_ for _ in ()).throw(ValueError("invalid number")))
    except ProtocolError:
        raise
    except (UnicodeError, json.JSONDecodeError, ValueError) as exc:
        raise ProtocolError("PROTOCOL_ERROR", "invalid JSON packet") from exc
    if not isinstance(value, dict):
        raise ProtocolError("PROTOCOL_ERROR", "packet must be a JSON object")
    return value


def encode_json(value: dict[str, Any], *, response: bool = False) -> bytes:
    packet = json.dumps(value, ensure_ascii=True, separators=(",", ":"), sort_keys=True).encode("ascii")
    limit = MAX_RESPONSE_BYTES if response else MAX_PACKET_BYTES
    if len(packet) > limit:
        raise ProtocolError("OUTPUT_TOO_LARGE", "packet exceeds protocol limit")
    return packet


def validate_request(value: dict[str, Any]) -> dict[str, Any]:
    if set(value) != {"version", "request_id", "operation", "args", "sequence", "challenge"}:
        raise ProtocolError("INVALID_REQUEST", "request has unexpected or missing fields")
    if type(value["version"]) is not int or value["version"] != PROTOCOL_VERSION:
        raise ProtocolError("PROTOCOL_ERROR", "unsupported protocol version")
    try:
        parsed = uuid.UUID(value["request_id"])
    except (ValueError, TypeError, AttributeError) as exc:
        raise ProtocolError("INVALID_REQUEST", "request_id must be a canonical UUID") from exc
    if str(parsed) != value["request_id"]:
        raise ProtocolError("INVALID_REQUEST", "request_id must be a canonical UUID")
    if not isinstance(value["operation"], str) or value["operation"] not in OPERATIONS:
        raise ProtocolError("OP_NOT_ALLOWED", "operation is not allowlisted")
    if not isinstance(value["args"], dict) or type(value["sequence"]) is not int or value["sequence"] != 1:
        raise ProtocolError("INVALID_REQUEST", "invalid args or sequence")
    if not isinstance(value["challenge"], str) or not re.fullmatch(r"[A-Za-z0-9_-]{40,64}", value["challenge"]):
        raise ProtocolError("INVALID_REQUEST", "invalid challenge")
    return value


def validate_response(value: dict[str, Any], request_id: str) -> dict[str, Any]:
    if set(value) != {"version", "request_id", "ok", "result", "error", "environment"}:
        raise ProtocolError("PROTOCOL_ERROR", "response has unexpected or missing fields")
    if type(value["version"]) is not int or value["version"] != PROTOCOL_VERSION:
        raise ProtocolError("PROTOCOL_ERROR", "unsupported response version")
    if value["request_id"] != request_id or type(value["ok"]) is not bool:
        raise ProtocolError("PROTOCOL_ERROR", "response does not match request")
    if not isinstance(value["environment"], dict):
        raise ProtocolError("PROTOCOL_ERROR", "response environment is missing")
    if value["ok"]:
        if not isinstance(value["result"], dict) or value["error"] is not None:
            raise ProtocolError("PROTOCOL_ERROR", "malformed successful response")
    else:
        error = value["error"]
        if value["result"] is not None or not isinstance(error, dict) or set(error) != {"code", "message"}:
            raise ProtocolError("PROTOCOL_ERROR", "malformed error response")
        if not isinstance(error["code"], str) or error["code"] not in ERROR_CODES or not isinstance(error["message"], str):
            raise ProtocolError("PROTOCOL_ERROR", "invalid error code or message")
    return value

