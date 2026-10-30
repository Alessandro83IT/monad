"""Single-request Unix SOCK_SEQPACKET server for the read-only broker."""

from __future__ import annotations

import collections
import hmac
import json
import os
import re
import secrets
import socket
import stat
import struct
import sys
import threading
import time
import uuid
from typing import Any

from . import MAX_PACKET_BYTES, MAX_RESPONSE_BYTES, PROTOCOL_VERSION
from .operations import HostOperations
from .protocol import ProtocolError, decode_json, encode_json, validate_request

SOCKET_PATH = "/run/monad-local-broker/broker.sock"
CONNECTION_TIMEOUT = 3.0
OPERATION_DEADLINE = 8.0
MAX_ACTIVE_CONNECTIONS = 4
MAX_REQUESTS_PER_MINUTE = 60
REPLAY_CACHE_SIZE = 4096
REPLAY_CACHE_SECONDS = 600.0


def peer_credentials(connection: socket.socket) -> tuple[int, int, int]:
    """Return kernel-supplied (pid, uid, gid), never a client JSON claim."""
    raw = connection.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, struct.calcsize("3i"))
    pid, uid, gid = struct.unpack("3i", raw)
    if pid <= 0:
        raise ProtocolError("AUTH_ERROR", "kernel peer credentials unavailable")
    return pid, uid, gid


class LocalBroker:
    def __init__(self, operations: Any | None = None, *, allowed_uid: int = 1000,
                 required_peer_namespaces: dict[str, str] | None = None,
                 expected_environment: dict[str, str] | None = None):
        self.operations = operations or HostOperations(expected_environment)
        self.allowed_uid = allowed_uid
        self.required_peer_namespaces = required_peer_namespaces or {
            name: os.readlink(f"/proc/self/ns/{name}") for name in ("pid", "user")
        }
        self.instance_id = str(uuid.uuid4())
        self._seen: collections.OrderedDict[str, float] = collections.OrderedDict()
        self._request_times: collections.deque[float] = collections.deque()
        self._lock = threading.Lock()

    def _error_response(self, request_id: str | None, error: ProtocolError,
                        environment: dict[str, Any] | None = None) -> dict[str, Any]:
        return {"version": PROTOCOL_VERSION, "request_id": request_id,
                "ok": False, "result": None,
                "error": {"code": error.code, "message": str(error)[:240]},
                "environment": environment or {}}

    def _remember(self, request_id: str) -> bool:
        now = time.monotonic()
        with self._lock:
            while self._seen and now - next(iter(self._seen.values())) > REPLAY_CACHE_SECONDS:
                self._seen.popitem(last=False)
            if request_id in self._seen:
                return False
            while self._request_times and now - self._request_times[0] > 60:
                self._request_times.popleft()
            if len(self._request_times) >= MAX_REQUESTS_PER_MINUTE:
                raise ProtocolError("RATE_LIMIT", "request rate limit exceeded")
            if len(self._seen) >= REPLAY_CACHE_SIZE:
                raise ProtocolError("RATE_LIMIT", "replay cache is full; fail-closed until entries expire")
            self._seen[request_id] = now
            self._request_times.append(now)
            return True

    def _check_peer(self, pid: int, uid: int) -> None:
        if uid != self.allowed_uid:
            raise ProtocolError("AUTH_ERROR", "peer UID is not authorized")
        try:
            # Bind authentication to a process in the broker's host PID and user
            # namespaces. This rejects a same-numbered UID from an agent sandbox.
            for namespace, expected in self.required_peer_namespaces.items():
                actual = os.readlink(f"/proc/{pid}/ns/{namespace}")
                if actual != expected:
                    raise ProtocolError("AUTH_ERROR", "peer is outside the authorized host namespace")
        except OSError as exc:
            raise ProtocolError("AUTH_ERROR", "peer identity could not be verified") from exc

    def handle(self, connection: socket.socket) -> None:
        """Serve exactly one challenge-bound request on a connected seqpacket socket."""
        connection.settimeout(CONNECTION_TIMEOUT)
        request_id: str | None = None
        operation = "unknown"
        peer: tuple[int, int, int] | None = None
        status = "error"
        started = time.monotonic()
        try:
            peer = peer_credentials(connection)
            self._check_peer(peer[0], peer[1])
            challenge = secrets.token_urlsafe(32)
            connection.sendall(encode_json({"version": PROTOCOL_VERSION, "challenge": challenge,
                                            "broker_instance": self.instance_id}))
            packet = connection.recv(MAX_PACKET_BYTES + 1)
            if not packet:
                raise ProtocolError("PROTOCOL_ERROR", "empty request")
            request = validate_request(decode_json(packet))
            request_id = request["request_id"]
            operation = request["operation"]
            if not hmac.compare_digest(request["challenge"], challenge):
                raise ProtocolError("REPLAY", "challenge is stale or does not match this connection")
            if not self._remember(request_id):
                raise ProtocolError("REPLAY", "request identifier was already used")
            # Strict per-operation schemas are enforced by the host operation layer.
            if time.monotonic() - started > CONNECTION_TIMEOUT:
                raise ProtocolError("TIMEOUT", "request deadline expired")
            operation_started = time.monotonic()
            result = self.operations.execute(operation, request["args"])
            if time.monotonic() - operation_started > OPERATION_DEADLINE:
                raise ProtocolError("TIMEOUT", "operation deadline expired")
            response = {"version": PROTOCOL_VERSION, "request_id": request_id,
                        "ok": True, "result": result, "error": None,
                        "environment": self.operations.environment_identity()}
            encoded = encode_json(response, response=True)
            connection.sendall(encoded)
            status = "ok"
        except socket.timeout:
            response = self._error_response(request_id, ProtocolError("TIMEOUT", "request timed out"),
                                            self._identity_or_empty())
            self._send_error(connection, response)
        except ProtocolError as exc:
            response = self._error_response(request_id, exc, self._identity_or_empty())
            self._send_error(connection, response)
            status = exc.code
        except Exception:
            response = self._error_response(request_id, ProtocolError("INTERNAL_ERROR", "broker operation failed"),
                                            self._identity_or_empty())
            self._send_error(connection, response)
            status = "INTERNAL_ERROR"
        finally:
            elapsed_ms = int((time.monotonic() - started) * 1000)
            audit = {"event": "broker_request", "request_id": request_id,
                     "operation": operation, "status": status, "elapsed_ms": elapsed_ms}
            if peer is not None:
                audit["peer"] = {"pid": peer[0], "uid": peer[1], "gid": peer[2]}
            print(json.dumps(audit, separators=(",", ":"), sort_keys=True), file=sys.stderr, flush=True)

    def _identity_or_empty(self) -> dict[str, Any]:
        try:
            return self.operations.environment_identity()
        except Exception:
            return {}

    @staticmethod
    def _send_error(connection: socket.socket, response: dict[str, Any]) -> None:
        try:
            encoded = encode_json(response, response=True)
        except ProtocolError:
            encoded = encode_json({"version": PROTOCOL_VERSION, "request_id": response.get("request_id"),
                                   "ok": False, "result": None,
                                   "error": {"code": "OUTPUT_TOO_LARGE", "message": "response exceeds protocol limit"},
                                   "environment": {}} , response=True)
        try:
            connection.sendall(encoded)
        except OSError:
            pass


class BrokerServer:
    def __init__(self, path: str = SOCKET_PATH, *, allowed_uid: int = 1000,
                 allowed_gid: int = 1000, operations: Any | None = None):
        self.path = path
        self.allowed_uid = allowed_uid
        self.allowed_gid = allowed_gid
        self.broker = LocalBroker(operations, allowed_uid=allowed_uid)
        self._active = threading.BoundedSemaphore(MAX_ACTIVE_CONNECTIONS)

    def _serve_connection(self, connection: socket.socket) -> None:
        try:
            with connection:
                self.broker.handle(connection)
        finally:
            self._active.release()

    def serve_forever(self) -> None:
        if os.geteuid() != 0:
            raise RuntimeError("host broker must run as root; unprivileged execution is not a trusted VM probe")
        if self.path != SOCKET_PATH:
            raise RuntimeError("broker socket path is fixed by the reviewed protocol")
        if not hasattr(socket, "SOCK_SEQPACKET"):
            raise RuntimeError("SOCK_SEQPACKET is unavailable; refusing unsafe fallback")
        parent = os.path.dirname(self.path)
        parent_st = os.lstat(parent)
        if not os.path.isdir(parent) or os.path.islink(parent) or parent_st.st_uid != 0 or (parent_st.st_mode & 0o022):
            raise RuntimeError("broker runtime directory must be a protected root-owned directory")
        try:
            os.lstat(self.path)
        except FileNotFoundError:
            pass
        else:
            raise RuntimeError("broker socket path already exists; refusing to replace it")
        server = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET | socket.SOCK_CLOEXEC)
        try:
            server.bind(self.path)
            os.chmod(self.path, 0o660, follow_symlinks=False)
            os.chown(self.path, 0, self.allowed_gid, follow_symlinks=False)
            server.listen(32)
            while True:
                connection, _ = server.accept()
                if not self._active.acquire(blocking=False):
                    connection.close()
                    continue
                threading.Thread(target=self._serve_connection, args=(connection,), daemon=True).start()
        finally:
            server.close()


def main() -> int:
    import tomllib
    try:
        if os.geteuid() != 0:
            raise ValueError("host broker must run as root")
        config_dir = os.lstat("/etc/monad-local-broker")
        if not stat.S_ISDIR(config_dir.st_mode) or config_dir.st_uid != 0 or config_dir.st_mode & 0o022:
            raise ValueError("broker config directory is not protected")
        fd = os.open("/etc/monad-local-broker/config.toml", os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
        with os.fdopen(fd, "rb") as stream:
            st = os.fstat(stream.fileno())
            if not stat.S_ISREG(st.st_mode) or st.st_uid != 0 or st.st_mode & 0o022 or st.st_size > 4096:
                raise ValueError("broker config must be a small, root-owned, non-writable regular file")
            config = tomllib.load(stream)
        if set(config) != {"broker"} or set(config["broker"]) != {
                "socket_path", "allowed_uid", "allowed_gid", "expected_virt", "expected_pid1",
                "expected_machine_id_sha256"}:
            raise ValueError("unexpected broker configuration schema")
        b = config["broker"]
        if b["socket_path"] != SOCKET_PATH or type(b["allowed_uid"]) is not int or type(b["allowed_gid"]) is not int:
            raise ValueError("invalid fixed broker configuration")
        if (not isinstance(b["expected_machine_id_sha256"], str)
                or not re.fullmatch(r"[0-9a-f]{64}", b["expected_machine_id_sha256"])
                or not isinstance(b["expected_virt"], str)
                or not isinstance(b["expected_pid1"], str)):
            raise ValueError("exact reviewed machine identity pin is required")
        ops = HostOperations({"virt": b["expected_virt"], "pid1_comm": b["expected_pid1"],
                              "machine_id_sha256": b["expected_machine_id_sha256"]})
        ops.verify_environment()
        BrokerServer(b["socket_path"], allowed_uid=b["allowed_uid"],
                     allowed_gid=b["allowed_gid"], operations=ops).serve_forever()
    except Exception as exc:
        print(f"local broker stopped fail-closed: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
