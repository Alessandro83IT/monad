"""Minimal stdio MCP adapter; every tool call is forwarded to the fixed broker."""

from __future__ import annotations

import json
import os
import select
import sys
import time
from typing import Any

from . import MAX_RESPONSE_BYTES
from .client import BrokerClient, BrokerRemoteError
from .protocol import ProtocolError

ARTIFACT_IDS = ["runner", "support", "sudoers", "canary_stage", "build_dir", "canary_receipt"]
FIXTURE_IDS = ["alpha_m1_ext4", "d6_residual"]
TOOLS = [
    {"name": "systemd_status", "description": "Read systemd overall state on the verified VM.", "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False}},
    {"name": "failed_units", "description": "List failed systemd unit names and states.", "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False}},
    {"name": "artifact_metadata", "description": "Read metadata for one fixed administrative artifact identifier.", "inputSchema": {"type": "object", "properties": {"artifact": {"type": "string", "enum": ARTIFACT_IDS}}, "required": ["artifact"], "additionalProperties": False}},
    {"name": "artifact_hashes", "description": "Read SHA-256 and size for one fixed administrative artifact; never returns file contents.", "inputSchema": {"type": "object", "properties": {"artifact": {"type": "string", "enum": ARTIFACT_IDS}}, "required": ["artifact"], "additionalProperties": False}},
    {"name": "fixture_mount", "description": "Read mount metadata for one fixed fixture identifier.", "inputSchema": {"type": "object", "properties": {"fixture": {"type": "string", "enum": FIXTURE_IDS}}, "required": ["fixture"], "additionalProperties": False}},
    {"name": "fixture_inventory", "description": "List bounded metadata for entries in one fixed fixture; never reads file contents.", "inputSchema": {"type": "object", "properties": {"fixture": {"type": "string", "enum": FIXTURE_IDS}}, "required": ["fixture"], "additionalProperties": False}},
    {"name": "build_inventory", "description": "Statically parse the protected CTest inventory without executing CMake or tests.", "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False}},
    {"name": "environment_identity", "description": "Read host VM, boot, kernel and namespace identity evidence.", "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False}},
]


class MCPServer:
    def __init__(self, client: BrokerClient | None = None):
        self.client = client or BrokerClient()
        self.tools = {tool["name"]: tool for tool in TOOLS}
        self._input = bytearray()

    def _read_message(self) -> dict[str, Any] | None:
        fd = sys.stdin.fileno()
        partial_deadline: float | None = None

        def take_line() -> bytes | None:
            nonlocal partial_deadline
            while b"\n" not in self._input:
                timeout = None if partial_deadline is None else max(0.0, partial_deadline - time.monotonic())
                ready, _, _ = select.select([fd], [], [], timeout)
                if not ready:
                    raise TimeoutError("incomplete MCP frame")
                chunk = os.read(fd, 4096)
                if not chunk:
                    return None if not self._input else (_ for _ in ()).throw(ValueError("incomplete MCP header"))
                if partial_deadline is None:
                    partial_deadline = time.monotonic() + 5.0
                self._input.extend(chunk)
                if len(self._input) > 8192 and b"\n" not in self._input:
                    raise ValueError("MCP header too large")
            line, _, rest = self._input.partition(b"\n")
            self._input[:] = rest
            return line + b"\n"

        headers = {}
        total = 0
        while True:
            line = take_line()
            if not line:
                return None
            total += len(line)
            if total > 8192:
                raise ValueError("MCP header too large")
            if line in (b"\r\n", b"\n"):
                break
            key, sep, value = line.decode("ascii", "strict").partition(":")
            if not sep or key.lower() in headers:
                raise ValueError("invalid MCP header")
            headers[key.lower()] = value.strip()
        if set(headers) != {"content-length"} or not headers["content-length"].isdigit():
            raise ValueError("invalid Content-Length")
        size = int(headers["content-length"])
        if size > MAX_RESPONSE_BYTES:
            raise ValueError("MCP body too large")
        deadline = time.monotonic() + 5.0
        while len(self._input) < size:
            timeout = max(0.0, deadline - time.monotonic())
            ready, _, _ = select.select([fd], [], [], timeout)
            if not ready:
                raise TimeoutError("incomplete MCP body")
            chunk = os.read(fd, min(4096, size - len(self._input)))
            if not chunk:
                raise ValueError("incomplete MCP body")
            self._input.extend(chunk)
        body = bytes(self._input[:size])
        del self._input[:size]
        value = json.loads(body.decode("utf-8", "strict"))
        if not isinstance(value, dict):
            raise ValueError("MCP message must be an object")
        return value

    @staticmethod
    def _write_message(value: dict[str, Any]) -> None:
        body = json.dumps(value, ensure_ascii=True, separators=(",", ":")).encode("ascii")
        if len(body) > MAX_RESPONSE_BYTES:
            body = json.dumps({"jsonrpc": "2.0", "id": value.get("id"),
                               "error": {"code": -32603, "message": "MCP output limit exceeded"}},
                              separators=(",", ":")).encode("ascii")
        sys.stdout.buffer.write(f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body)
        sys.stdout.buffer.flush()

    def handle(self, message: dict[str, Any]) -> dict[str, Any] | None:
        if message.get("jsonrpc") != "2.0" or not isinstance(message.get("method"), str):
            return {"jsonrpc": "2.0", "id": message.get("id"),
                    "error": {"code": -32600, "message": "invalid JSON-RPC request"}}
        method = message["method"]
        request_id = message.get("id")
        params = message.get("params", {})
        if not isinstance(params, dict):
            return {"jsonrpc": "2.0", "id": request_id,
                    "error": {"code": -32602, "message": "invalid params"}}
        if method == "notifications/initialized":
            return None
        if method == "initialize":
            protocol = params.get("protocolVersion", "2024-11-05")
            if not isinstance(protocol, str):
                return {"jsonrpc": "2.0", "id": request_id,
                        "error": {"code": -32602, "message": "invalid protocolVersion"}}
            return {"jsonrpc": "2.0", "id": request_id,
                    "result": {"protocolVersion": protocol, "capabilities": {"tools": {"listChanged": False}},
                               "serverInfo": {"name": "monad-local-verification-broker", "version": "1.0.0"}}}
        if method == "ping":
            return {"jsonrpc": "2.0", "id": request_id, "result": {}}
        if method == "tools/list":
            return {"jsonrpc": "2.0", "id": request_id, "result": {"tools": TOOLS}}
        if method != "tools/call":
            return {"jsonrpc": "2.0", "id": request_id,
                    "error": {"code": -32601, "message": "method not found"}}
        name = params.get("name")
        arguments = params.get("arguments", {})
        tool = self.tools.get(name) if isinstance(name, str) else None
        if tool is None or not isinstance(arguments, dict):
            return {"jsonrpc": "2.0", "id": request_id,
                    "error": {"code": -32602, "message": "unknown tool or invalid arguments"}}
        operation_args = arguments
        try:
            result = self.client.call(name, operation_args)
            content = [{"type": "text", "text": json.dumps(result, ensure_ascii=True, separators=(",", ":"))}]
            return {"jsonrpc": "2.0", "id": request_id, "result": {"content": content, "isError": False}}
        except (BrokerRemoteError, ProtocolError) as exc:
            return {"jsonrpc": "2.0", "id": request_id,
                    "result": {"content": [{"type": "text", "text": json.dumps({"error": getattr(exc, "code", "PROTOCOL_ERROR"), "message": str(exc)})}],
                               "isError": True}}

    def serve(self) -> int:
        while True:
            try:
                message = self._read_message()
                if message is None:
                    return 0
                response = self.handle(message)
                if response is not None:
                    self._write_message(response)
            except Exception as exc:
                print(f"local broker MCP protocol error: {type(exc).__name__}", file=sys.stderr, flush=True)
                return 1


if __name__ == "__main__":
    raise SystemExit(MCPServer().serve())
