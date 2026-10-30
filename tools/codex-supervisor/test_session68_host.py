"""Synthetic safety checks for the Session 68 host driver (never runs Codex)."""
from pathlib import Path
import subprocess
import unittest


SCRIPT = Path(__file__).with_name("session68-host.sh")


class Session68HostDriverTests(unittest.TestCase):
    def test_shell_syntax(self):
        result = subprocess.run(["bash", "-n", str(SCRIPT)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_help_does_not_start_or_read_credentials(self):
        result = subprocess.run([str(SCRIPT), "--help"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--staged-auth FILE", result.stdout)
        self.assertIn("never copies, prints, or reads credential contents", result.stdout)

    def test_requires_explicit_authorization_before_host_actions(self):
        result = subprocess.run([str(SCRIPT)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("explicit --authorize is required", result.stderr)
        self.assertNotIn("codex", result.stderr.lower().replace("codex executable", ""))

    def test_driver_is_one_shot_read_only_and_removes_only_staged_file(self):
        source = SCRIPT.read_text()
        self.assertIn('MONAD_CODEX_SANDBOX=read-only', source)
        self.assertIn('authorize-plan session68-readonly-smoke', source)
        self.assertEqual(source.count(' run session68-readonly-smoke '), 1)
        self.assertIn('rm -- "$staged_auth"', source)
        self.assertNotIn('rm -rf', source)
        self.assertNotIn('resume ', source)
        self.assertNotIn('install -m', source)
        self.assertIn('residual-process-group', source)
        self.assertIn('unset OPENAI_API_KEY MONAD_CODEX_HOME', source)


if __name__ == "__main__":
    unittest.main()
