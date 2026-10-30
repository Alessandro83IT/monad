import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import urllib.error

import github_orchestrator as flow


class Response:
    status = 200
    headers = {"Content-Type": "application/json"}

    def __init__(self, value):
        self.raw = value if isinstance(value, bytes) else json.dumps(value).encode()

    def __enter__(self): return self
    def __exit__(self, *args): return False
    def getcode(self): return self.status
    def read(self, size=-1): return self.raw[:size]


def issue(number=7, body='{"schema":1,"milestones":[]}'):
    return {"repository_url": "https://api.github.com/repos/Alessandro83IT/monad",
            "number": number, "state": "open", "body": body, "pull_request": None}


class GitHubOrchestrationTests(unittest.TestCase):
    def test_fetch_is_fixed_repo_bounded_get_and_snapshot_shape(self):
        calls = []
        def opener(request, timeout):
            calls.append((request, timeout))
            return Response(issue())
        snapshot = flow.fetch_issue(7, opener=opener)
        request, timeout = calls[0]
        self.assertEqual("GET", request.get_method())
        self.assertEqual(flow.API + "7", request.full_url)
        self.assertEqual(flow.TIMEOUT_SECONDS, timeout)
        self.assertEqual("Alessandro83IT/monad", snapshot["repository"]["full_name"])
        self.assertEqual(7, snapshot["issue"]["number"])

    def test_invalid_issue_number_rejected_without_network(self):
        for number in (0, -1, True, 2**40):
            with self.subTest(number=number), self.assertRaises(flow.FetchError):
                flow.fetch_issue(number, opener=mock.Mock())

    def test_timeout_and_network_errors_are_sanitized(self):
        for error in (TimeoutError("token=do-not-print"), OSError("secret material")):
            with self.subTest(error=type(error).__name__), self.assertRaises(flow.FetchError) as raised:
                flow.fetch_issue(7, opener=mock.Mock(side_effect=error))
            self.assertNotIn("do-not-print", str(raised.exception))
            self.assertNotIn("secret material", str(raised.exception))

    def test_rate_limit_and_unexpected_http_are_rejected(self):
        headers = {"X-RateLimit-Remaining": "0"}
        error = urllib.error.HTTPError(flow.API + "7", 403, "forbidden", headers, io.BytesIO())
        with self.assertRaisesRegex(flow.FetchError, "rate limit"):
            flow.fetch_issue(7, opener=mock.Mock(side_effect=error))

    def test_unexpected_content_type_and_oversized_response_rejected(self):
        response = Response(issue()); response.headers = {"Content-Type": "text/html"}
        with self.assertRaisesRegex(flow.FetchError, "content type"):
            flow.fetch_issue(7, opener=mock.Mock(return_value=response))
        response = Response(b"x" * (flow.MAX_RESPONSE + 1))
        with self.assertRaisesRegex(flow.FetchError, "size limit"):
            flow.fetch_issue(7, opener=mock.Mock(return_value=response))

    def test_response_identity_number_state_and_body_are_validated(self):
        malformed = [
            {**issue(), "repository_url": "https://api.github.com/repos/attacker/monad"},
            {**issue(), "number": 8}, {**issue(), "number": True},
            {**issue(), "state": "mystery"}, {**issue(), "body": None},
            {**issue(), "pull_request": "yes"},
        ]
        for value in malformed:
            with self.subTest(value=value), self.assertRaises(flow.FetchError):
                flow.fetch_issue(7, opener=mock.Mock(return_value=Response(value)))

    def test_duplicate_keys_and_bad_json_rejected(self):
        for payload in (b'{"number":7,"number":7}', b"not-json", b'{"x":NaN}'):
            with self.subTest(payload=payload), self.assertRaises(flow.FetchError):
                flow.fetch_issue(7, opener=mock.Mock(return_value=Response(payload)))

    def test_local_authorization_is_issue_bound_and_exclusive(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); snapshot_path = root / "snapshot.json"; auth = root / "auth.json"
            body = '{"schema":1,"milestones":[]}'
            snapshot_path.write_text(json.dumps({"repository":{"full_name":"Alessandro83IT/monad"},
                "issue":{"number":7,"state":"open","body":body,"pull_request":None}}))
            flow.authorize(snapshot_path, auth)
            value = json.loads(auth.read_text())
            self.assertEqual(hashlib.sha256(body.encode()).hexdigest(), value["body_sha256"])
            self.assertEqual(0o600, auth.stat().st_mode & 0o777)
            with self.assertRaises(FileExistsError): flow.authorize(snapshot_path, auth)
            changed = json.loads(snapshot_path.read_text()); changed["issue"]["body"] += " "
            snapshot_path.write_text(json.dumps(changed))
            import github_issue_adapter as adapter
            with self.assertRaisesRegex(ValueError, "does not bind"):
                adapter.adapt(snapshot_path, auth)

    def test_remote_issue_cannot_claim_local_authorization(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); snapshot_path = root / "snapshot.json"; auth = root / "auth.json"
            snapshot_path.write_text(json.dumps({"repository":{"full_name":"attacker/monad"},
                "issue":{"number":1,"state":"open","body":"{}","pull_request":None}}))
            with self.assertRaisesRegex(ValueError, "not allowed"):
                flow.authorize(snapshot_path, auth)
            self.assertFalse(auth.exists())


if __name__ == "__main__": unittest.main()
