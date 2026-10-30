#!/usr/bin/env python3
"""Unprivileged protocol and operation tests using temporary fixtures only."""

from __future__ import annotations

import json
import os
import pathlib
import socket
import tempfile
import threading
import time
import unittest
import uuid
from unittest import mock

from qualification.local_broker import PROTOCOL_VERSION
from qualification.local_broker.broker import LocalBroker
from qualification.local_broker.client import BrokerClient, BrokerRemoteError
from qualification.local_broker.mcp_server import MCPServer
from qualification.local_broker.operations import (
    HostOperations, _build_inventory, _fixture_inventory, _hash_path,
    _metadata, _mount_info, _run_fixed, _safe_open,
)
from qualification.local_broker.protocol import ProtocolError, decode_json, encode_json, validate_request

ENV = {"virt": "oracle", "pid1_comm": "systemd", "boot_id": "test-boot", "namespaces": {}}


class FakeOperations:
    def __init__(self, *, identity=None, result=None, delay=0.0, error=None):
        self.identity = identity or dict(ENV)
        self.result = result or {"state": "ok"}
        self.delay = delay
        self.error = error
        self.calls = []

    def environment_identity(self):
        return self.identity

    def execute(self, operation, args):
        self.calls.append((operation, args))
        if self.delay:
            time.sleep(self.delay)
        if self.error:
            raise self.error
        return {"operation": operation, "args": args, **self.result}


class LocalSocketHarness:
    def __init__(self, operations=None, *, allowed_uid=None, namespaces=None):
        self.directory = tempfile.TemporaryDirectory(prefix="monad-broker-test-")
        self.path = str(pathlib.Path(self.directory.name) / "broker.sock")
        self.listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.listener.bind(self.path)
        self.listener.listen(8)
        self.broker = LocalBroker(operations or FakeOperations(),
                                  allowed_uid=os.getuid() if allowed_uid is None else allowed_uid,
                                  required_peer_namespaces=namespaces or {
                                      n: os.readlink(f"/proc/self/ns/{n}") for n in ("pid", "user")})
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    def _serve(self):
        while not self.stop_event.is_set():
            self.listener.settimeout(0.1)
            try:
                conn, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            with conn:
                self.broker.handle(conn)

    def client(self, **kwargs):
        return BrokerClient(socket_path=self.path, expected_server_uid=os.getuid(),
                            expected_environment={"virt": "oracle", "pid1_comm": "systemd"},
                            check_path=False, **kwargs)

    def close(self):
        self.stop_event.set()
        self.listener.close()
        self.thread.join(timeout=1)
        self.directory.cleanup()


class ProtocolTests(unittest.TestCase):
    def test_duplicate_json_keys_rejected(self):
        with self.assertRaisesRegex(ProtocolError, "duplicate"):
            decode_json(b'{"version":1,"version":1}')

    def test_oversized_packet_rejected(self):
        from qualification.local_broker import MAX_PACKET_BYTES
        with self.assertRaises(ProtocolError) as caught:
            decode_json(b" " * (MAX_PACKET_BYTES + 1))
        self.assertEqual(caught.exception.code, "OUTPUT_TOO_LARGE")

    def test_unknown_operation_and_extra_fields_rejected(self):
        base = {"version": 1, "request_id": str(uuid.uuid4()), "operation": "systemd_status",
                "args": {}, "sequence": 1, "challenge": "a" * 43}
        malformed = dict(base, operation="shell")
        with self.assertRaises(ProtocolError):
            validate_request(malformed)
        with self.assertRaises(ProtocolError):
            validate_request(dict(base, path="/etc/shadow"))

    def test_bad_uuid_version_sequence_and_challenge(self):
        base = {"version": 1, "request_id": str(uuid.uuid4()), "operation": "systemd_status",
                "args": {}, "sequence": 1, "challenge": "a" * 43}
        for key, value in (("request_id", "not-a-uuid"), ("version", True),
                           ("sequence", 2), ("challenge", "stale")):
            with self.subTest(key=key), self.assertRaises(ProtocolError):
                validate_request(dict(base, **{key: value}))


class BrokerTests(unittest.TestCase):
    def setUp(self):
        self.ops = FakeOperations()
        self.harness = LocalSocketHarness(self.ops)

    def tearDown(self):
        self.harness.close()

    def test_allowlisted_operation_round_trip(self):
        result = self.harness.client().call("systemd_status")
        self.assertEqual(result["environment"]["virt"], "oracle")
        self.assertEqual(result["result"]["operation"], "systemd_status")
        self.assertEqual(len(self.ops.calls), 1)

    def test_disallowed_operation_never_reaches_broker(self):
        with self.assertRaises(ProtocolError):
            self.harness.client().call("run_shell")
        self.assertEqual(self.ops.calls, [])

    def test_duplicate_request_id_is_rejected(self):
        request_id = str(uuid.uuid4())
        self.harness.client().call("systemd_status", request_id=request_id)
        with self.assertRaises(BrokerRemoteError) as caught:
            self.harness.client().call("systemd_status", request_id=request_id)
        self.assertEqual(caught.exception.code, "REPLAY")
        self.assertEqual(len(self.ops.calls), 1)

    def test_wrong_peer_uid_is_denied(self):
        denied = LocalSocketHarness(FakeOperations(), allowed_uid=os.getuid() + 1)
        try:
            with self.assertRaises((ProtocolError, BrokerRemoteError)):
                denied.client().call("systemd_status")
        finally:
            denied.close()

    def test_wrong_peer_namespace_is_denied(self):
        denied = LocalSocketHarness(FakeOperations(), namespaces={"user": "user:[0]", "pid": "pid:[0]"})
        try:
            with self.assertRaises((ProtocolError, BrokerRemoteError)):
                denied.client().call("systemd_status")
        finally:
            denied.close()

    def test_mismatched_host_identity_is_rejected(self):
        mismatch = LocalSocketHarness(FakeOperations(identity={"virt": "container", "pid1_comm": "python"}))
        try:
            with self.assertRaises(ProtocolError) as caught:
                mismatch.client().call("systemd_status")
            self.assertEqual(caught.exception.code, "ENVIRONMENT_MISMATCH")
        finally:
            mismatch.close()

    def test_operation_timeout_is_reported(self):
        slow = LocalSocketHarness(FakeOperations(error=ProtocolError("TIMEOUT", "fixed operation timeout")))
        try:
            with self.assertRaises(BrokerRemoteError) as caught:
                slow.client().call("systemd_status")
            self.assertEqual(caught.exception.code, "TIMEOUT")
        finally:
            slow.close()

    def test_client_timeout(self):
        slow = LocalSocketHarness(FakeOperations(delay=0.2))
        try:
            with self.assertRaises(BrokerRemoteError) as caught:
                slow.client(timeout=0.03).call("systemd_status")
            self.assertEqual(caught.exception.code, "TIMEOUT")
        finally:
            slow.close()

    def test_excessive_result_is_replaced_with_bounded_error(self):
        large = LocalSocketHarness(FakeOperations(result={"payload": "x" * 70000}))
        try:
            with self.assertRaises(BrokerRemoteError) as caught:
                large.client().call("systemd_status")
            self.assertEqual(caught.exception.code, "OUTPUT_TOO_LARGE")
        finally:
            large.close()

    def test_replay_of_old_challenge_is_denied(self):
        peer, server = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        thread = threading.Thread(target=self.harness.broker.handle, args=(server,), daemon=True)
        thread.start()
        hello = decode_json(peer.recv(1024))
        request = {"version": PROTOCOL_VERSION, "request_id": str(uuid.uuid4()),
                   "operation": "systemd_status", "args": {}, "sequence": 1,
                   "challenge": "B" * 43}
        peer.sendall(encode_json(request))
        response = decode_json(peer.recv(4096), response=True)
        self.assertEqual(response["error"]["code"], "REPLAY")
        self.assertTrue(hello["challenge"] != request["challenge"])
        peer.close(); thread.join(timeout=1); server.close()

    def test_response_request_id_tampering_is_rejected(self):
        directory = tempfile.TemporaryDirectory(prefix="monad-broker-tamper-")
        path = str(pathlib.Path(directory.name) / "sock")
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        listener.bind(path); listener.listen(1)
        def fake():
            conn, _ = listener.accept()
            with conn:
                conn.sendall(encode_json({"version": 1, "challenge": "C" * 43, "broker_instance": "test"}))
                decode_json(conn.recv(1024))
                conn.sendall(encode_json({"version": 1, "request_id": str(uuid.uuid4()), "ok": True,
                                          "result": {}, "error": None, "environment": ENV}, response=True))
        thread = threading.Thread(target=fake, daemon=True); thread.start()
        client = BrokerClient(socket_path=path, expected_server_uid=os.getuid(),
                              expected_environment={"virt": "oracle", "pid1_comm": "systemd"},
                              check_path=False)
        try:
            with self.assertRaises((ProtocolError, BrokerRemoteError)):
                client.call("systemd_status")
        finally:
            listener.close(); directory.cleanup()

    def test_incomplete_response_fails_closed(self):
        directory = tempfile.TemporaryDirectory(prefix="monad-broker-incomplete-")
        path = str(pathlib.Path(directory.name) / "sock")
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        listener.bind(path); listener.listen(1)
        def fake():
            conn, _ = listener.accept()
            with conn:
                conn.sendall(encode_json({"version": 1, "challenge": "D" * 43, "broker_instance": "test"}))
                conn.recv(1024)
        thread = threading.Thread(target=fake, daemon=True); thread.start()
        client = BrokerClient(socket_path=path, expected_server_uid=os.getuid(),
                              expected_environment={"virt": "oracle", "pid1_comm": "systemd"}, check_path=False)
        try:
            with self.assertRaises(ProtocolError):
                client.call("systemd_status")
        finally:
            listener.close(); directory.cleanup()

    def test_broker_interruption_fails_closed(self):
        # Closing after challenge simulates broker termination before the response.
        directory = tempfile.TemporaryDirectory(prefix="monad-broker-stop-")
        path = str(pathlib.Path(directory.name) / "sock")
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        listener.bind(path); listener.listen(1)
        def stop():
            conn, _ = listener.accept()
            with conn:
                conn.sendall(encode_json({"version": 1, "challenge": "E" * 43, "broker_instance": "test"}))
        thread = threading.Thread(target=stop, daemon=True); thread.start()
        client = BrokerClient(socket_path=path, expected_server_uid=os.getuid(),
                              expected_environment={"virt": "oracle", "pid1_comm": "systemd"}, check_path=False)
        try:
            with self.assertRaises((ProtocolError, BrokerRemoteError)):
                client.call("systemd_status")
        finally:
            listener.close(); directory.cleanup()


class OperationsTests(unittest.TestCase):
    def test_metadata_and_hash_are_content_free_and_candidate_pinned(self):
        with tempfile.TemporaryDirectory(prefix="monad-broker-artifact-") as temp:
            path = pathlib.Path(temp) / "artifact"
            path.write_bytes(b"reviewed fixture")
            import hashlib
            digest = hashlib.sha256(b"reviewed fixture").hexdigest()
            with mock.patch.dict("qualification.local_broker.operations.EXPECTED_CANDIDATE_HASHES",
                                 {"fixture": digest}):
                metadata = _metadata(path)
                hashed = _hash_path(path, "fixture")
            self.assertEqual(metadata["type"], "regular")
            self.assertNotIn("content", metadata)
            self.assertEqual(hashed["sha256"], digest)
            self.assertTrue(hashed["matches_candidate"])

    def test_fixed_system_queries_use_only_fixed_argv(self):
        operations = HostOperations({"virt": "oracle", "pid1_comm": "systemd"})
        with mock.patch("qualification.local_broker.operations._environment_identity", return_value=ENV):
            with mock.patch("qualification.local_broker.operations._run_fixed", return_value=(1, "degraded")) as run:
                result = operations.execute("systemd_status", {})
            self.assertEqual(result["state"], "degraded")
            run.assert_called_once_with(["/usr/bin/systemctl", "is-system-running"])

    def test_fixed_runner_bounds_output_and_timeout(self):
        result = _run_fixed(["/usr/bin/printf", "ok"], timeout=1, output_limit=8)
        self.assertEqual(result, (0, "ok"))
        with self.assertRaisesRegex(ProtocolError, "output exceeded"):
            _run_fixed(["/usr/bin/printf", "overflow"], timeout=1, output_limit=3)
        with self.assertRaisesRegex(ProtocolError, "timed out"):
            _run_fixed(["/usr/bin/sleep", "1"], timeout=0.01, output_limit=8)

    def test_host_operation_schema_rejects_extra_paths_and_unknown_artifacts(self):
        operations = HostOperations({"virt": "oracle", "pid1_comm": "systemd"})
        with mock.patch("qualification.local_broker.operations._environment_identity", return_value=ENV):
            with self.assertRaises(ProtocolError) as caught:
                operations.execute("systemd_status", {"path": "/etc/passwd"})
            self.assertEqual(caught.exception.code, "INVALID_REQUEST")
            with self.assertRaises(ProtocolError) as caught:
                operations.execute("artifact_hashes", {"artifact": "../../etc/shadow"})
            self.assertEqual(caught.exception.code, "INVALID_REQUEST")

    def test_symlink_and_traversal_are_rejected(self):
        with tempfile.TemporaryDirectory(prefix="monad-broker-path-") as temp:
            root = pathlib.Path(temp)
            target = root / "real"; target.mkdir()
            link = root / "link"; link.symlink_to(target, target_is_directory=True)
            with self.assertRaises(ProtocolError) as caught:
                _safe_open(link / "child", directory=True)
            self.assertIn(caught.exception.code, {"SYMLINK", "NOT_FOUND"})
            with self.assertRaises(ProtocolError):
                _safe_open(pathlib.Path("/tmp/../etc/passwd"))

    def test_missing_and_inaccessible_paths_have_explicit_errors(self):
        with self.assertRaises(ProtocolError) as caught:
            _safe_open(pathlib.Path("/definitely-not-a-monad-broker-path"))
        self.assertEqual(caught.exception.code, "NOT_FOUND")

    def test_fixture_inventory_never_follows_symlink(self):
        with tempfile.TemporaryDirectory(prefix="monad-broker-fixture-") as temp:
            root = pathlib.Path(temp); (root / "evidence.txt").write_text("preserved")
            (root / "link").symlink_to("/etc/passwd")
            with mock.patch.dict("qualification.local_broker.operations.FIXTURES", {"test": root}):
                result = _fixture_inventory(root)
            self.assertEqual(result["count"], 2)
            self.assertEqual(next(e["type"] for e in result["entries"] if e["name"] == "link"), "symlink")

    def test_mount_info_identifies_pid1_namespace_source(self):
        with tempfile.TemporaryDirectory(prefix="monad-broker-mount-") as temp:
            root = pathlib.Path(temp)
            mount_line = "36 25 8:1 / / rw,relatime - ext4 /dev/sda1 rw\n"
            with mock.patch("builtins.open", mock.mock_open(read_data=mount_line)):
                with mock.patch("qualification.local_broker.operations.os.readlink", return_value="mnt:[4026531840]"):
                    result = _mount_info(root)
            self.assertEqual(result["view"], "pid1_mount_namespace")
            self.assertEqual(result["target"], "/")
            self.assertTrue(result["mount_namespace"].startswith("mnt:["))

    def test_build_inventory_static_parser(self):
        with tempfile.TemporaryDirectory(prefix="monad-broker-build-") as temp:
            root = pathlib.Path(temp)
            executable = root / "tool"; executable.write_bytes(b"fixture"); executable.chmod(0o555)
            (root / "CTestTestfile.cmake").write_text(
                'add_test([=[ordinary_python_check]=] "/usr/bin/python3" "check.py")\n'
                'add_test([=[safe_test]=] "' + str(executable) + '")\n'
                'set_tests_properties([=[safe_test]=] PROPERTIES LABELS "privileged;slow")\n')
            with mock.patch.dict("qualification.local_broker.operations.ARTIFACTS", {"build_dir": root}):
                result = _build_inventory()
            self.assertEqual(result["count"], 1)
            self.assertEqual(result["tests"][0]["labels"], ["privileged", "slow"])
            self.assertTrue(result["tests"][0]["executable_present"])
            self.assertEqual(result["count"], 1)

    def test_build_inventory_refuses_unrecognized_test_syntax(self):
        with tempfile.TemporaryDirectory(prefix="monad-broker-build-bad-") as temp:
            root = pathlib.Path(temp); (root / "CTestTestfile.cmake").write_text('add_test(\n')
            with mock.patch.dict("qualification.local_broker.operations.ARTIFACTS", {"build_dir": root}):
                with self.assertRaises(ProtocolError):
                    _build_inventory()

    def test_build_inventory_does_not_silently_omit_subdirectories(self):
        with tempfile.TemporaryDirectory(prefix="monad-broker-build-subdirs-") as temp:
            root = pathlib.Path(temp)
            (root / "CTestTestfile.cmake").write_text('subdirs("tests")\n')
            with mock.patch.dict("qualification.local_broker.operations.ARTIFACTS", {"build_dir": root}):
                with self.assertRaisesRegex(ProtocolError, "subdirectories"):
                    _build_inventory()


class MCPTests(unittest.TestCase):
    def test_tools_are_closed_allowlist(self):
        server = MCPServer(FakeMCPClient())
        listed = server.handle({"jsonrpc": "2.0", "id": 1, "method": "tools/list"})
        self.assertEqual(len(listed["result"]["tools"]), 8)
        denied = server.handle({"jsonrpc": "2.0", "id": 2, "method": "tools/call",
                                "params": {"name": "shell", "arguments": {"cmd": "id"}}})
        self.assertIn("error", denied)


class FakeMCPClient:
    def call(self, operation, args):
        return {"operation": operation, "args": args}


if __name__ == "__main__":
    unittest.main(verbosity=2)
