from __future__ import annotations

import hashlib
import importlib.util
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

BOOTSTRAP_PATH = Path(__file__).resolve().parents[1] / "qualification" / "alpha-m1-verified-execution-bootstrap.py"
_BOOTSTRAP_SPEC = importlib.util.spec_from_file_location("verified_execution_bootstrap", BOOTSTRAP_PATH)
bootstrap = importlib.util.module_from_spec(_BOOTSTRAP_SPEC)
assert _BOOTSTRAP_SPEC and _BOOTSTRAP_SPEC.loader
sys.modules[_BOOTSTRAP_SPEC.name] = bootstrap
_BOOTSTRAP_SPEC.loader.exec_module(bootstrap)

# This exact inline loader is the first trust boundary documented for manual
# launch. It authenticates stdin bytes before compiling the second-stage file.
INLINE_LOADER = (
    "import hashlib,sys\n"
    "expected=sys.argv[1]\n"
    "source=sys.stdin.buffer.read(262145)\n"
    "if len(source)>262144 or sys.stdin.buffer.read(1):\n"
    " print(\"ERROR: bootstrap source exceeds size limit\",file=sys.stderr);raise SystemExit(70)\n"
    "actual=hashlib.sha256(source).hexdigest()\n"
    "if actual != expected:\n"
    " print(\"ERROR: bootstrap SHA-256 mismatch\",file=sys.stderr);raise SystemExit(70)\n"
    "args=sys.argv[1:]\n"
    "sys.argv=[\"<verified-bootstrap>\",*args]\n"
    "ns={\"__name__\":\"__main__\",\"__file__\":\"<verified-bootstrap>\",\"__package__\":None,\"__alpha_m1_verified_bootstrap_sha256__\":actual}\n"
    "exec(compile(source,\"<verified-bootstrap>\",\"exec\",dont_inherit=True),ns,ns)\n"
)
EXPECTED_INLINE_LOADER_SHA256 = "feef002511128263daf5fa05fcaa72686a688d58fb717b2f8d1a9d026b205021"


class VerifiedExecutionBootstrapTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.qual = self.root / "qualification"
        self.qual.mkdir()
        self.runner_path = self.qual / bootstrap.EXPECTED_RUNNER_BASENAME

    def write_runner(self, source: bytes) -> tuple[Path, str]:
        self.runner_path.write_bytes(source)
        self.runner_path.chmod(0o600)
        return self.runner_path, hashlib.sha256(source).hexdigest()

    def test_inline_loader_matches_reviewed_command_digest(self):
        self.assertEqual(hashlib.sha256(INLINE_LOADER.encode()).hexdigest(),
                         EXPECTED_INLINE_LOADER_SHA256)

    def run_outer(self, runner_path: Path, runner_digest: str, *, bootstrap_source=None,
                  bootstrap_digest=None, runner_args=(), cwd=None, env=None):
        source = BOOTSTRAP_PATH.read_bytes() if bootstrap_source is None else bootstrap_source
        digest = hashlib.sha256(source).hexdigest() if bootstrap_digest is None else bootstrap_digest
        child_env = {"PATH": "/usr/bin:/bin", "LANG": "C", "LC_ALL": "C", "TZ": "UTC"}
        if env:
            child_env.update(env)
        proc = subprocess.Popen(
            [sys.executable, "-I", "-S", "-c", INLINE_LOADER,
             digest, runner_digest, str(runner_path), *runner_args],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            cwd=cwd or self.root, env=child_env,
        )
        out, err = proc.communicate(source, timeout=10)
        return proc.returncode, out, err

    def test_valid_digest_executes_exact_verified_bytes(self):
        marker = self.root / "marker"
        source = f"from pathlib import Path\nPath({str(marker)!r}).write_bytes(b'verified')\n".encode()
        path, digest = self.write_runner(source)
        verified = bootstrap.read_verified_source(str(path), digest)
        bootstrap.execute_verified_source(verified, [])
        self.assertEqual(marker.read_bytes(), b"verified")

    def test_wrong_digest_never_executes_payload(self):
        marker = self.root / "must-not-exist"
        path, _ = self.write_runner(f"open({str(marker)!r},'w').write('bad')\n".encode())
        with self.assertRaisesRegex(bootstrap.BootstrapError, "SHA-256 mismatch"):
            bootstrap.read_verified_source(str(path), "0" * 64)
        self.assertFalse(marker.exists())

    def test_missing_file_fails_closed(self):
        with self.assertRaises(OSError):
            bootstrap.read_verified_source(str(self.runner_path), "0" * 64)

    def test_final_symlink_rejected(self):
        other = self.root / "payload"
        other.write_text("pass\n")
        self.runner_path.symlink_to(other)
        with self.assertRaises(OSError):
            bootstrap.read_verified_source(str(self.runner_path), hashlib.sha256(other.read_bytes()).hexdigest())

    def test_parent_symlink_rejected(self):
        linked_root = self.root / "linked-root"
        linked_root.mkdir()
        (linked_root / "qualification").symlink_to(self.qual, target_is_directory=True)
        linked = linked_root / "qualification" / bootstrap.EXPECTED_RUNNER_BASENAME
        with self.assertRaises(OSError):
            bootstrap.read_verified_source(str(linked), "0" * 64)

    def test_special_file_rejected_without_blocking(self):
        os.mkfifo(self.runner_path)
        with self.assertRaisesRegex(bootstrap.BootstrapError, "regular file"):
            bootstrap.read_verified_source(str(self.runner_path), "0" * 64)

    def test_replacement_before_open_fails_digest(self):
        original = b"open('unused-marker','w').write('original')\n"
        path, expected = self.write_runner(original)
        replacement = self.root / "replacement"
        replacement.write_bytes(b"open('attacker-marker','w').write('attacker')\n")
        os.replace(replacement, path)
        with self.assertRaisesRegex(bootstrap.BootstrapError, "SHA-256 mismatch"):
            bootstrap.read_verified_source(str(path), expected)
        self.assertFalse((self.root / "attacker-marker").exists())

    def test_replacement_after_verification_does_not_change_executed_bytes(self):
        original_marker = self.root / "verified-marker"
        attacker_marker = self.root / "attacker-marker"
        original = f"from pathlib import Path\nPath({str(original_marker)!r}).write_text('verified')\n".encode()
        path, digest = self.write_runner(original)
        verified = bootstrap.read_verified_source(str(path), digest)
        path.write_text(f"open({str(attacker_marker)!r},'w').write('attacker')\n")
        bootstrap.execute_verified_source(verified, [])
        self.assertEqual(original_marker.read_text(), "verified")
        self.assertFalse(attacker_marker.exists())

    def test_concurrent_content_mutation_is_rejected(self):
        original = b"#" + (b"A" * 200_000) + b"\n"
        path, expected = self.write_runner(original)
        real_read = bootstrap.os.read
        changed = False

        def mutate_during_read(fd, size):
            nonlocal changed
            chunk = real_read(fd, size)
            if chunk and not changed:
                changed = True
                with path.open("r+b") as stream:
                    stream.seek(80_000)
                    stream.write(b"Z" * 128)
                    stream.flush()
                    os.fsync(stream.fileno())
            return chunk

        with mock.patch.object(bootstrap.os, "read", side_effect=mutate_during_read):
            with self.assertRaises(bootstrap.BootstrapError):
                bootstrap.read_verified_source(str(path), expected)
        self.assertTrue(changed)

    def test_open_descriptor_is_closed_after_capture(self):
        path, digest = self.write_runner(b"pass\n")
        real_open = bootstrap.os.open
        captured = []

        def capture_open(file, flags, *args, **kwargs):
            fd = real_open(file, flags, *args, **kwargs)
            if file == bootstrap.EXPECTED_RUNNER_BASENAME:
                captured.append(fd)
            return fd

        with mock.patch.object(bootstrap.os, "open", side_effect=capture_open):
            bootstrap.read_verified_source(str(path), digest)
        self.assertEqual(len(captured), 1)
        with self.assertRaises(OSError):
            os.fstat(captured[0])

    def test_interrupted_read_is_retried(self):
        path, digest = self.write_runner(b"pass\n")
        real_read = bootstrap.os.read
        calls = 0

        def interrupt_once(fd, size):
            nonlocal calls
            calls += 1
            if calls == 1:
                raise InterruptedError()
            return real_read(fd, size)

        with mock.patch.object(bootstrap.os, "read", side_effect=interrupt_once):
            verified = bootstrap.read_verified_source(str(path), digest)
        self.assertEqual(verified.source, b"pass\n")
        self.assertGreaterEqual(calls, 2)

    def test_relative_and_traversal_paths_rejected(self):
        digest = hashlib.sha256(b"pass\n").hexdigest()
        for bad in ("qualification/alpha-m1-verified-evidence-runner.py",
                    str(self.runner_path.parent / ".." / "qualification" / self.runner_path.name)):
            with self.subTest(path=bad), self.assertRaises(bootstrap.BootstrapError):
                bootstrap.read_verified_source(bad, digest)

    def test_syntax_error_is_before_payload_execution(self):
        marker = self.root / "must-not-exist"
        source = f"open({str(marker)!r},'w').write('partial')\nif True print('broken')\n".encode()
        path, digest = self.write_runner(source)
        verified = bootstrap.read_verified_source(str(path), digest)
        with self.assertRaises(SyntaxError):
            bootstrap.execute_verified_source(verified, [])
        self.assertFalse(marker.exists())

    def test_bootstrap_digest_failure_prevents_second_stage_execution(self):
        marker = self.root / "bootstrap-marker"
        malicious_bootstrap = f"open({str(marker)!r},'w').write('ran')\n".encode()
        path, runner_digest = self.write_runner(b"pass\n")
        code, _out, err = self.run_outer(path, runner_digest, bootstrap_source=malicious_bootstrap,
                                         bootstrap_digest="f" * 64)
        self.assertEqual(code, 70)
        self.assertIn(b"bootstrap SHA-256 mismatch", err)
        self.assertFalse(marker.exists())

    def test_oversized_bootstrap_is_rejected_before_compile(self):
        marker = self.root / "oversized-bootstrap-marker"
        oversized = b"#" * (262_145 - 1) + f"\nopen({str(marker)!r},'w').write('ran')\n".encode()
        path, runner_digest = self.write_runner(b"pass\n")
        code, _out, err = self.run_outer(path, runner_digest, bootstrap_source=oversized)
        self.assertEqual(code, 70)
        self.assertIn(b"size limit", err)
        self.assertFalse(marker.exists())

    def test_end_to_end_verified_bootstrap_runs_only_synthetic_runner(self):
        marker = self.root / "end-to-end-marker"
        payload = f"from pathlib import Path\nPath({str(marker)!r}).write_text(__file__)\n".encode()
        path, digest = self.write_runner(payload)
        bootstrap_digest = hashlib.sha256(BOOTSTRAP_PATH.read_bytes()).hexdigest()
        code, _out, err = self.run_outer(path, digest, bootstrap_digest=bootstrap_digest)
        self.assertEqual(code, 0, err.decode())
        self.assertEqual(marker.read_text(), str(path))

    def test_bootstrap_rejects_runner_digest_mismatch_end_to_end(self):
        marker = self.root / "runner-mismatch-marker"
        payload = f"open({str(marker)!r},'w').write('ran')\n".encode()
        path, _digest = self.write_runner(payload)
        code, _out, err = self.run_outer(path, "0" * 64)
        self.assertEqual(code, 70)
        self.assertIn(b"runner SHA-256 mismatch", err)
        self.assertFalse(marker.exists())

    def test_isolated_python_does_not_import_cwd_or_pythonpath_module(self):
        marker = self.root / "unexpected-import-ran"
        evil_module = self.root / "unexpected_session42_module.py"
        evil_module.write_text(f"open({str(marker)!r},'w').write('imported')\n")
        payload = b"import unexpected_session42_module\n"
        path, digest = self.write_runner(payload)
        code, _out, err = self.run_outer(path, digest, cwd=self.root,
                                         env={"PYTHONPATH": str(self.root), "PYTHONHOME": str(self.root)})
        self.assertEqual(code, 70)
        self.assertIn(b"No module named", err)
        self.assertFalse(marker.exists())

    def test_signal_handler_in_verified_synthetic_runner(self):
        ready = self.root / "signal-ready"
        handled = self.root / "signal-handled"
        source = (
            "import signal,time\n"
            f"open({str(ready)!r},'w').close()\n"
            "def on_term(*_):\n"
            f" open({str(handled)!r},'w').close()\n"
            " raise SystemExit(143)\n"
            "signal.signal(signal.SIGTERM,on_term)\n"
            "while True: time.sleep(.05)\n"
        ).encode()
        path, digest = self.write_runner(source)
        proc = subprocess.Popen(
            [sys.executable, "-I", "-S", "-c", INLINE_LOADER,
             hashlib.sha256(BOOTSTRAP_PATH.read_bytes()).hexdigest(), digest, str(path)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            cwd=self.root, env={"PATH": "/usr/bin:/bin", "LC_ALL": "C"},
        )
        assert proc.stdin
        proc.stdin.write(BOOTSTRAP_PATH.read_bytes())
        proc.stdin.close()
        proc.stdin = None
        deadline = time.monotonic() + 5
        while not ready.exists() and proc.poll() is None and time.monotonic() < deadline:
            time.sleep(.01)
        self.assertTrue(ready.exists(), "synthetic verified runner did not start")
        proc.send_signal(signal.SIGTERM)
        out, err = proc.communicate(timeout=5)
        self.assertEqual(proc.returncode, 143, (out, err))
        self.assertTrue(handled.exists())

    def test_runner_cli_refuses_unverified_path_invocation(self):
        real_runner = Path(__file__).resolve().parents[1] / "qualification" / "alpha-m1-verified-evidence-runner.py"
        proc = subprocess.run([sys.executable, "-I", str(real_runner)], capture_output=True, timeout=5)
        self.assertEqual(proc.returncode, 70)
        self.assertIn(b"verified-execution bootstrap", proc.stderr)


if __name__ == "__main__":
    unittest.main()
