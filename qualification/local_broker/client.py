"""Unprivileged client for the fixed Monad local broker socket."""

from __future__ import annotations

import os
import re
import socket
import stat
import struct
import uuid
from typing import Any

from . import MAX_PACKET_BYTES, MAX_RESPONSE_BYTES, PROTOCOL_VERSION
from .broker import SOCKET_PATH
from .protocol import OPERATIONS, ProtocolError, decode_json, encode_json, validate_response


class BrokerRemoteError(RuntimeError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


class BrokerClient:
    def __init__(self, *, socket_path: str = SOCKET_PATH, expected_server_uid: int = 0,
                 expected_environment: dict[str, str] | None = None, timeout: float = 11.0,
                 check_path: bool = True):
        self.socket_path = socket_path
        self.expected_server_uid = expected_server_uid
        if expected_environment is None:
            machine_pin = os.environ.get("MONAD_BROKER_MACHINE_ID_SHA256", "")
            if not re.fullmatch(r"[0-9a-f]{64}", machine_pin):
                raise ValueError("MONAD_BROKER_MACHINE_ID_SHA256 must contain the reviewed VM identity pin")
            expected_environment = {"virt": "oracle", "pid1_comm": "systemd",
                                   "machine_id_sha256": machine_pin}
        self.expected_environment = expected_environment
        self.timeout = timeout
        self.check_path = check_path

    def _check_socket_path(self) -> None:
        if not self.check_path:
            return
        path = os.path.abspath(self.socket_path)
        parts = path.strip("/").split("/")
        current = "/"
        for part in parts[:-1]:
            current = os.path.join(current, part)
            st = os.lstat(current)
            if not stat.S_ISDIR(st.st_mode) or stat.S_ISLNK(st.st_mode) or st.st_uid != 0 or st.st_mode & 0o022:
                raise ProtocolError("AUTH_ERROR", "broker socket parent is not protected")
        st = os.lstat(path)
        if not stat.S_ISSOCK(st.st_mode) or stat.S_ISLNK(st.st_mode) or st.st_uid != 0:
            raise ProtocolError("AUTH_ERROR", "broker socket identity is unsafe")
        if st.st_mode & 0o007:
            raise ProtocolError("AUTH_ERROR", "broker socket grants access to other users")

    @staticmethod
    def _peer_uid(sock: socket.socket) -> int:
        raw = sock.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, struct.calcsize("3i"))
        _, uid, _ = struct.unpack("3i", raw)
        return uid

    def call(self, operation: str, args: dict[str, Any] | None = None,
             *, request_id: str | None = None) -> dict[str, Any]:
        if operation not in OPERATIONS:
            raise ProtocolError("OP_NOT_ALLOWED", "operation is not allowlisted")
        args = {} if args is None else args
        request_id = str(uuid.uuid4()) if request_id is None else request_id
        self._check_socket_path()
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET | socket.SOCK_CLOEXEC)
        sock.settimeout(self.timeout)
        try:
            sock.connect(self.socket_path)
            if self._peer_uid(sock) != self.expected_server_uid:
                raise ProtocolError("AUTH_ERROR", "broker peer UID does not match configured host broker")
            challenge_packet = sock.recv(MAX_PACKET_BYTES + 1)
            challenge_message = decode_json(challenge_packet)
            if (set(challenge_message) != {"version", "challenge", "broker_instance"}
                    or type(challenge_message["version"]) is not int
                    or challenge_message["version"] != PROTOCOL_VERSION
                    or not isinstance(challenge_message["challenge"], str)
                    or not re.fullmatch(r"[A-Za-z0-9_-]{40,64}", challenge_message["challenge"])
                    or not isinstance(challenge_message["broker_instance"], str)):
                raise ProtocolError("PROTOCOL_ERROR", "invalid broker challenge")
            request = {"version": PROTOCOL_VERSION, "request_id": request_id,
                       "operation": operation, "args": args, "sequence": 1,
                       "challenge": challenge_message["challenge"]}
            sock.sendall(encode_json(request))
            response_packet = sock.recv(MAX_RESPONSE_BYTES + 1)
            if len(response_packet) > MAX_RESPONSE_BYTES:
                raise ProtocolError("OUTPUT_TOO_LARGE", "broker response exceeds limit")
            response = validate_response(decode_json(response_packet, response=True), request_id)
            environment = response["environment"]
            if any(environment.get(key) != value for key, value in self.expected_environment.items()):
                raise ProtocolError("ENVIRONMENT_MISMATCH", "response is not from the configured VM")
            if not response["ok"]:
                error = response["error"]
                raise BrokerRemoteError(error["code"], error["message"])
            return {"request_id": request_id, "broker_instance": challenge_message["broker_instance"],
                    "environment": environment, "result": response["result"]}
        except socket.timeout as exc:
            raise BrokerRemoteError("TIMEOUT", "broker did not respond before deadline") from exc
        except OSError as exc:
            raise BrokerRemoteError("UNAVAILABLE", "broker transport unavailable") from exc
        finally:
            sock.close()

