"""Fail-closed local nonce ledger for synthetic candidate launch tests.

The ledger gives cooperating launchers serialized, durable single-use nonce
consumption. It does not provide rollback resistance against an owner who can
restore an older directory image.
"""
from __future__ import annotations

import fcntl
import hashlib
import hmac
import json
import os
import re
import secrets
import stat
import time
from typing import Any

from .contracts import InvalidRequest
from .trust_chain import canonical_json, strict_json

LEDGER_NAME = "nonce-ledger.json"
LOCK_NAME = "nonce-ledger.lock"
MAX_LEDGER_BYTES = 1024 * 1024
MAX_NONCES = 4096
NONCE_RE = re.compile(r"[A-Za-z0-9_-]{22,96}\Z")


def _write_all(fd: int, data: bytes) -> None:
    view = memoryview(data)
    while view:
        written = os.write(fd, view)
        if written <= 0:
            raise OSError("short write while updating nonce ledger")
        view = view[written:]


class NonceLedger:
    def __init__(self, directory_fd: int, *, root_anchor: bytes, timeout_seconds: float = 2.0):
        if not isinstance(root_anchor, bytes) or len(root_anchor) < 32:
            raise InvalidRequest("independent ledger root key is required")
        if not 0 < timeout_seconds <= 30:
            raise InvalidRequest("ledger lock timeout is outside fixed bounds")
        self._dir_fd = os.dup(directory_fd)
        self._key = hmac.new(root_anchor, b"alpha-m1-nonce-ledger-v1", hashlib.sha256).digest()
        self._timeout = timeout_seconds

    def close(self) -> None:
        if self._dir_fd >= 0:
            os.close(self._dir_fd)
            self._dir_fd = -1

    def __enter__(self) -> "NonceLedger":
        self._lock_fd = -1
        try:
            info = os.fstat(self._dir_fd)
            if (not stat.S_ISDIR(info.st_mode) or info.st_uid != os.geteuid() or
                    stat.S_IMODE(info.st_mode) != 0o700):
                raise InvalidRequest("ledger directory must be owner-controlled mode 0700")
            self._lock_fd = os.open(LOCK_NAME, os.O_RDWR | os.O_CLOEXEC | os.O_NOFOLLOW,
                                    dir_fd=self._dir_fd)
            self._check_file(self._lock_fd)
            deadline = time.monotonic() + self._timeout
            while True:
                try:
                    fcntl.flock(self._lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    break
                except BlockingIOError:
                    if time.monotonic() >= deadline:
                        raise InvalidRequest("nonce ledger is busy") from None
                    time.sleep(0.01)
            self._load()  # Corruption is discovered before any authorization is consumed.
            return self
        except BaseException:
            if self._lock_fd >= 0:
                try:
                    fcntl.flock(self._lock_fd, fcntl.LOCK_UN)
                except OSError:
                    pass
                os.close(self._lock_fd)
                self._lock_fd = -1
            self.close()
            raise

    def __exit__(self, *_: object) -> None:
        lock_fd = getattr(self, "_lock_fd", -1)
        try:
            if lock_fd >= 0:
                try:
                    fcntl.flock(lock_fd, fcntl.LOCK_UN)
                finally:
                    os.close(lock_fd)
                    self._lock_fd = -1
        finally:
            self.close()

    @staticmethod
    def _check_file(fd: int) -> os.stat_result:
        info = os.fstat(fd)
        if (not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or
                info.st_uid != os.geteuid() or stat.S_IMODE(info.st_mode) != 0o600):
            raise InvalidRequest("ledger file type, ownership, link count, or mode is unsafe")
        return info

    def _read(self) -> bytes:
        fd = os.open(LEDGER_NAME, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK,
                     dir_fd=self._dir_fd)
        try:
            before = self._check_file(fd)
            if before.st_size > MAX_LEDGER_BYTES:
                raise InvalidRequest("nonce ledger exceeds its fixed size limit")
            raw = bytearray()
            while len(raw) < before.st_size:
                block = os.read(fd, min(65536, before.st_size - len(raw)))
                if not block:
                    raise InvalidRequest("nonce ledger ended during read")
                raw.extend(block)
            after = self._check_file(fd)
            if (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (
                    after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns):
                raise InvalidRequest("nonce ledger changed during read")
            return bytes(raw)
        finally:
            os.close(fd)

    def _seal(self, rows: list[dict[str, Any]]) -> bytes:
        unsigned = {"schema_version": 1, "nonces": rows}
        mac = hmac.new(self._key, canonical_json(unsigned), hashlib.sha256).hexdigest()
        return canonical_json({**unsigned, "mac": mac}) + b"\n"

    def _load(self) -> list[dict[str, Any]]:
        # A stale staging file indicates an interrupted/ambiguous prior update.
        try:
            os.stat(".nonce-ledger.next", dir_fd=self._dir_fd, follow_symlinks=False)
        except FileNotFoundError:
            pass
        else:
            raise InvalidRequest("stale nonce ledger staging file; manual review required")
        value = strict_json(self._read(), maximum=MAX_LEDGER_BYTES)
        if not isinstance(value, dict) or set(value) != {"schema_version", "nonces", "mac"} or value["schema_version"] != 1:
            raise InvalidRequest("nonce ledger schema is invalid")
        rows = value["nonces"]
        if not isinstance(rows, list) or len(rows) > MAX_NONCES:
            raise InvalidRequest("nonce ledger entry count is invalid")
        unsigned = {"schema_version": 1, "nonces": rows}
        expected = hmac.new(self._key, canonical_json(unsigned), hashlib.sha256).hexdigest()
        if not isinstance(value["mac"], str) or not hmac.compare_digest(expected, value["mac"]):
            raise InvalidRequest("nonce ledger authentication failed")
        seen: set[str] = set()
        for row in rows:
            if (not isinstance(row, dict) or set(row) != {"nonce", "state", "record_id"} or
                    not isinstance(row["nonce"], str) or not NONCE_RE.fullmatch(row["nonce"]) or
                    row["state"] not in {"CONSUMED", "FINISHED", "FAILED"} or
                    not isinstance(row["record_id"], str) or row["nonce"] in seen):
                raise InvalidRequest("nonce ledger entry is malformed or duplicated")
            seen.add(row["nonce"])
        return rows

    def _store(self, rows: list[dict[str, Any]]) -> None:
        raw = self._seal(rows)
        if len(raw) > MAX_LEDGER_BYTES:
            raise InvalidRequest("nonce ledger update exceeds fixed size limit")
        name = ".nonce-ledger.next"
        fd = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
                     0o600, dir_fd=self._dir_fd)
        try:
            _write_all(fd, raw)
            os.fsync(fd)
        except BaseException:
            os.close(fd)
            raise
        else:
            os.close(fd)
        os.rename(name, LEDGER_NAME, src_dir_fd=self._dir_fd, dst_dir_fd=self._dir_fd)
        os.fsync(self._dir_fd)

    def consume(self, nonce: str, record_id: str) -> None:
        if not isinstance(nonce, str) or not NONCE_RE.fullmatch(nonce):
            raise InvalidRequest("authorization nonce format is invalid")
        rows = self._load()
        if any(row["nonce"] == nonce for row in rows):
            raise InvalidRequest("authorization nonce has already been consumed")
        if len(rows) >= MAX_NONCES:
            raise InvalidRequest("nonce ledger is full; fail closed")
        rows.append({"nonce": nonce, "state": "CONSUMED", "record_id": record_id})
        self._store(rows)

    def finish(self, nonce: str, *, success: bool) -> None:
        rows = self._load()
        for row in rows:
            if row["nonce"] == nonce:
                if row["state"] != "CONSUMED":
                    raise InvalidRequest("nonce record is already terminal")
                row["state"] = "FINISHED" if success else "FAILED"
                self._store(rows)
                return
        raise InvalidRequest("consumed nonce record not found")


def initialize_synthetic_ledger(directory_fd: int, *, root_anchor: bytes) -> None:
    """Test fixture helper; intentionally not called by any launcher path."""
    key = hmac.new(root_anchor, b"alpha-m1-nonce-ledger-v1", hashlib.sha256).digest()
    raw = canonical_json({"schema_version": 1, "nonces": [],
                          "mac": hmac.new(key, canonical_json({"schema_version": 1, "nonces": []}), hashlib.sha256).hexdigest()}) + b"\n"
    fd = os.open(LEDGER_NAME, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
                 0o600, dir_fd=directory_fd)
    try:
        _write_all(fd, raw)
        os.fsync(fd)
    finally:
        os.close(fd)
    os.fsync(directory_fd)
