import hashlib
import io
import json
from contextlib import redirect_stdout
from pathlib import Path
import tempfile
import unittest

import github_issue_adapter as adapter


def valid_milestone():
    return {
        "id": "issue-task", "objective": "synthetic objective", "prompt": "synthetic prompt",
        "allowed_paths": ["tools/codex-supervisor/"],
        "tests": [["python3", "-m", "unittest", "discover", "-s", "tools/codex-supervisor", "-p", "test_*.py"]],
        "timeout_seconds": 120, "depends_on": [],
        "success": ["simulated CLI tests pass", "no protected artifacts change", "report exists"],
        "stop_conditions": ["CLI mismatch", "timeout", "stop request", "protected path change", "out-of-scope change"],
        "limits": {"max_timeout_seconds": 120, "max_changed_paths": 3, "max_output_bytes": 1000000},
    }


class GitHubIssueAdapterTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.body = json.dumps({"schema": 1, "milestones": [valid_milestone()]})
        self.snapshot = self.root / "snapshot.json"
        self.authorization = self.root / "authorization.json"
        self.write_snapshot()
        self.write_authorization()

    def tearDown(self):
        self.tmp.cleanup()

    def write_snapshot(self, *, body=None, number=17, state="open", pull_request=None, extra=None):
        issue = {"number": number, "state": state, "body": self.body if body is None else body,
                 "pull_request": pull_request}
        if extra:
            issue.update(extra)
        self.snapshot.write_text(json.dumps({"repository": {"full_name": "Alessandro83IT/monad"}, "issue": issue}))

    def write_authorization(self, *, body=None, repository="Alessandro83IT/monad", number=17, mode=0o600):
        value = {"schema": 1, "repository": repository, "issue_number": number,
                 "body_sha256": hashlib.sha256((self.body if body is None else body).encode()).hexdigest()}
        self.authorization.write_text(json.dumps(value))
        self.authorization.chmod(mode)

    def test_valid_authorized_issue_produces_plan(self):
        result = adapter.adapt(self.snapshot, self.authorization)
        self.assertEqual("issue-task", result["milestones"][0]["id"])

    def test_body_tampering_after_authorization_rejected(self):
        self.write_snapshot(body=self.body + " ")
        with self.assertRaisesRegex(ValueError, "does not bind"):
            adapter.adapt(self.snapshot, self.authorization)

    def test_wrong_repository_or_issue_number_rejected(self):
        self.write_authorization(repository="attacker/monad")
        with self.assertRaisesRegex(ValueError, "does not bind"):
            adapter.adapt(self.snapshot, self.authorization)
        self.write_authorization(number=18)
        with self.assertRaisesRegex(ValueError, "does not bind"):
            adapter.adapt(self.snapshot, self.authorization)

    def test_other_repository_snapshot_is_rejected_even_with_matching_auth(self):
        snapshot = json.loads(self.snapshot.read_text())
        snapshot["repository"]["full_name"] = "attacker/monad"
        self.snapshot.write_text(json.dumps(snapshot))
        self.write_authorization(repository="attacker/monad")
        with self.assertRaisesRegex(ValueError, "not allowed"):
            adapter.adapt(self.snapshot, self.authorization)

    def test_closed_issue_and_pull_request_rejected(self):
        self.write_snapshot(state="closed")
        with self.assertRaisesRegex(ValueError, "open issues"):
            adapter.adapt(self.snapshot, self.authorization)
        self.write_snapshot(pull_request={"url": "https://example.invalid"})
        with self.assertRaisesRegex(ValueError, "open issues"):
            adapter.adapt(self.snapshot, self.authorization)

    def test_unknown_snapshot_fields_rejected(self):
        self.write_snapshot(extra={"author": {"login": "trusted-looking"}})
        with self.assertRaisesRegex(ValueError, "unexpected fields"):
            adapter.adapt(self.snapshot, self.authorization)

    def test_duplicate_json_keys_rejected(self):
        self.snapshot.write_text('{"repository":{"full_name":"example/monad"},"issue":{"number":17,"state":"open","body":"x","pull_request":null,"state":"closed"}}')
        with self.assertRaisesRegex(ValueError, "duplicate"):
            adapter.adapt(self.snapshot, self.authorization)

    def test_boolean_issue_number_rejected(self):
        self.write_snapshot(number=True)
        with self.assertRaisesRegex(ValueError, "issue number"):
            adapter.adapt(self.snapshot, self.authorization)

    def test_insecure_or_symlink_authorization_rejected(self):
        self.write_authorization(mode=0o644)
        with self.assertRaisesRegex(ValueError, "mode 0600"):
            adapter.adapt(self.snapshot, self.authorization)
        target = self.root / "auth-target"
        target.write_text(self.authorization.read_text())
        self.authorization.unlink()
        self.authorization.symlink_to(target)
        with self.assertRaisesRegex(ValueError, "non-symlink"):
            adapter.adapt(self.snapshot, self.authorization)

    def test_arbitrary_command_and_protected_paths_rejected_by_supervisor_schema(self):
        bad = valid_milestone()
        bad["tests"] = [["sh", "-c", "touch /tmp/pwned"]]
        self.body = json.dumps({"schema": 1, "milestones": [bad]})
        self.write_snapshot()
        self.write_authorization()
        with self.assertRaisesRegex(ValueError, "only Python unittest"):
            adapter.adapt(self.snapshot, self.authorization)
        bad = valid_milestone()
        bad["allowed_paths"] = ["qualification/"]
        self.body = json.dumps({"schema": 1, "milestones": [bad]})
        self.write_snapshot()
        self.write_authorization()
        with self.assertRaisesRegex(ValueError, "protected path"):
            adapter.adapt(self.snapshot, self.authorization)

    def test_only_one_milestone_supported(self):
        second = valid_milestone()
        second["id"] = "second-task"
        self.body = json.dumps({"schema": 1, "milestones": [valid_milestone(), second]})
        self.write_snapshot()
        self.write_authorization()
        with self.assertRaisesRegex(ValueError, "exactly one"):
            adapter.adapt(self.snapshot, self.authorization)

    def test_oversized_input_rejected_before_json_parse(self):
        self.snapshot.write_text(" " * 300001)
        with self.assertRaisesRegex(ValueError, "size limit"):
            adapter.adapt(self.snapshot, self.authorization)

    def test_cli_dry_run_emits_json_without_running_supervisor(self):
        output = io.StringIO()
        with redirect_stdout(output):
            self.assertEqual(0, adapter.main(["--dry-run", "--snapshot", str(self.snapshot),
                                              "--authorization", str(self.authorization)]))
        self.assertEqual("issue-task", json.loads(output.getvalue())["milestones"][0]["id"])

    def test_cli_requires_dry_run(self):
        with self.assertRaises(SystemExit) as raised:
            adapter.main(["--snapshot", str(self.snapshot), "--authorization", str(self.authorization)])
        self.assertEqual(2, raised.exception.code)


if __name__ == "__main__":
    unittest.main()
