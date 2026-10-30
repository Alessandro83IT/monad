#!/usr/bin/env python3
"""Non-privileged consistency checks for the permanent Codex infrastructure."""

import ast
import pathlib
import re
import tomllib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def runner_allowlist():
    tree = ast.parse((ROOT / "qualification/athena-alpha-m1-runner.py").read_text())
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(
            isinstance(target, ast.Name) and target.id == "ALLOWLIST"
            for target in node.targets
        ):
            return ast.literal_eval(node.value)
    raise AssertionError("runner ALLOWLIST not found")


class AutonomousInfrastructureTest(unittest.TestCase):
    def test_project_agent_configuration_and_role_files_are_parseable(self):
        config = tomllib.loads((ROOT / ".codex/config.toml").read_text())
        self.assertEqual(config["agents"]["enabled"], True)
        self.assertEqual(config["agents"]["max_concurrent_threads_per_session"], 4)
        expected = {"core", "verification", "security", "operations"}
        actual = set()
        for path in (ROOT / ".codex/agents").glob("*.toml"):
            agent = tomllib.loads(path.read_text())
            self.assertTrue(agent["name"])
            self.assertTrue(agent["description"])
            self.assertTrue(agent["developer_instructions"])
            actual.add(agent["name"])
        self.assertEqual(actual, expected)

    def test_launcher_starts_codex_without_approval_bypass_flags(self):
        launcher = (ROOT / "scripts/codex-autonomous.sh").read_text()
        self.assertIn('exec codex "$@"', launcher)
        self.assertNotRegex(launcher, r"--dangerously-bypass-approvals|--yolo|approval_policy")

    def test_privileged_runner_boundary_matches_sudoers_and_orchestrator(self):
        allowlist = runner_allowlist()
        sudoers = (ROOT / "qualification/athena-alpha-m1-sudoers").read_text()
        orchestrator = ast.parse((ROOT / "qualification/run-alpha-m1-privileged.py").read_text())
        expected = None
        for node in orchestrator.body:
            if isinstance(node, ast.Assign) and any(
                isinstance(target, ast.Name) and target.id == "EXPECTED"
                for target in node.targets
            ):
                expected = ast.literal_eval(node.value)
                break
        sudo_cases = re.findall(r"athena-alpha-m1-runner ([a-z0-9_]+)", sudoers)
        self.assertEqual(len(allowlist), 18)
        self.assertEqual(set(allowlist), set(sudo_cases))
        self.assertEqual(set(allowlist), set(expected))

    def test_runner_uses_fixed_argv_and_has_no_shell_command_path(self):
        source = (ROOT / "qualification/athena-alpha-m1-runner.py").read_text()
        self.assertIn("authorize_case(sys.argv[1], ALLOWLIST)", source)
        self.assertNotRegex(source, r"shell\s*=\s*True")
        self.assertNotRegex(source, r"os\.system\s*\(")
        self.assertNotRegex(source, r"subprocess\.(?:call|run|Popen)\(\s*f['\"]")
        self.assertIn("os.O_NOFOLLOW", source)
        self.assertIn("stage_artifacts(", source)
        self.assertIn("wait_for_unit_termination(", source)
        self.assertIn("termination_confirmed", source)
        self.assertIn('"run_id": run_id', source)

    def test_policy_explicitly_gates_administrative_mutations(self):
        policy = (ROOT / "docs/security/authorization-policy.md").read_text()
        for phrase in (
            "Human approval required", "Modify sudoers", "install/update runner",
            "Expand privileges", "Delete fixture", "Roll back installed",
            "Push, publish, or release", "TOCTOU",
        ):
            self.assertIn(phrase, policy)

    def test_recovery_preserves_d6_and_rejects_hash_drift(self):
        recovery = (ROOT / "docs/qualification/alpha-m1-recovery.md").read_text()
        self.assertIn("D6 residual", recovery)
        self.assertIn("Historical reports have contained different", recovery)
        self.assertIn("INCOMPLETE", recovery)
        self.assertIn("Install or activate only after the separate administrative review", recovery)


if __name__ == "__main__":
    unittest.main(verbosity=2)
