from __future__ import annotations

import hashlib
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import signal
import subprocess
import threading
import tempfile
import time
import unittest
from unittest import mock

MODULE_PATH = Path(__file__).resolve().parents[1] / "qualification" / "alpha-m1-verified-evidence-runner.py"
spec = importlib.util.spec_from_file_location("verified_runner", MODULE_PATH)
runner = importlib.util.module_from_spec(spec)
assert spec.loader
spec.loader.exec_module(runner)


class VerifiedRunnerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def fixture(self, content=b"printf 'fixture-ok\\n'\n"):
        p = self.root / "collector.sh"
        p.write_bytes(content)
        p.chmod(0o600)
        return p

    def execute_fixture(self, fd, digest, *, collector_args, **kwargs):
        with mock.patch.object(runner, "EXPECTED_SHA256", digest):
            return runner.execute_snapshot(fd, collector_args=collector_args, **kwargs)

    def collector_args(self):
        return ["--output-parent", str(self.root), "--candidate-dir", str(self.root)]

    def test_valid_digest_and_seals(self):
        p = self.fixture()
        fd, digest = runner.snapshot_collector(p, hashlib.sha256(p.read_bytes()).hexdigest())
        self.addCleanup(os.close, fd)
        self.assertEqual(digest, hashlib.sha256(p.read_bytes()).hexdigest())
        import fcntl
        self.assertEqual(fcntl.fcntl(fd, fcntl.F_GET_SEALS) & runner.SEALS, runner.SEALS)
        runner._verify_sealed_snapshot(fd, digest)

    def test_unsealed_or_wrong_descriptor_is_rejected(self):
        raw_fd = os.memfd_create("unsealed-fixture", os.MFD_CLOEXEC | os.MFD_ALLOW_SEALING)
        try:
            os.write(raw_fd, b"not sealed")
            with self.assertRaisesRegex(runner.RunnerError, "lacks required immutable seals"):
                runner.execute_snapshot(raw_fd, collector_args=self.collector_args())
        finally:
            os.close(raw_fd)
        p = self.fixture()
        fd, digest = runner.snapshot_collector(p, hashlib.sha256(p.read_bytes()).hexdigest())
        self.addCleanup(os.close, fd)
        with mock.patch.object(runner, "EXPECTED_SHA256", "0" * 64):
            with self.assertRaisesRegex(runner.RunnerError, "digest does not match"):
                runner.execute_snapshot(fd, collector_args=self.collector_args())

    def test_wrong_digest_rejected(self):
        with self.assertRaisesRegex(runner.RunnerError, "mismatch"):
            runner.snapshot_collector(self.fixture(), "0" * 64)

    def test_missing_and_unreadable_file_rejected(self):
        with self.assertRaises(runner.RunnerError):
            runner.snapshot_collector(self.root / "missing")
        p = self.fixture()
        p.chmod(0)
        if os.access(p, os.R_OK):  # root-like test environments cannot model EACCES
            self.skipTest("mode-based unreadability is not enforceable for this uid")
        with self.assertRaises(runner.RunnerError):
            runner.snapshot_collector(p)

    def test_symlink_and_hardlink_rejected(self):
        p = self.fixture()
        link = self.root / "link"
        link.symlink_to(p)
        with self.assertRaisesRegex(runner.RunnerError, "symlink"):
            runner.snapshot_collector(link)
        hard = self.root / "hard"
        os.link(p, hard)
        with self.assertRaisesRegex(runner.RunnerError, "singly linked"):
            runner.snapshot_collector(p)

    def test_path_replacement_after_verification_cannot_change_executed_bytes(self):
        target = self.root / "result"
        p = self.fixture(f"printf original > {target}\n".encode())
        fd, digest = runner.snapshot_collector(p, hashlib.sha256(p.read_bytes()).hexdigest())
        self.addCleanup(os.close, fd)
        p.unlink()
        p.write_text("printf attacker\n")
        out = io.StringIO()
        status = self.execute_fixture(fd, digest, collector_args=self.collector_args(),
                                      record_stream=out)
        self.assertEqual(status, 0)
        out.seek(0)
        self.assertEqual(out.read().count(digest), 1)
        self.assertEqual(target.read_bytes(), b"original")

    def test_synthetic_memfd_execution_preserves_candidate_path_with_spaces_and_hashes_fd(self):
        candidate = self.root / "checkout with spaces" / "qualification"
        candidate.mkdir(parents=True)
        source_report = self.root / "source-path.txt"
        source_hash = self.root / "source-hash.txt"
        source = (
            f"printf '%s\\n' \"$0\" > {str(source_report)!r}\n"
            f"sha256sum -- \"$0\" > {str(source_hash)!r}\n"
            f"printf '%s\\n' \"$4\" > {str(self.root / 'candidate-path.txt')!r}\n"
        ).encode()
        fd, digest = runner.snapshot_collector(self.fixture(source), hashlib.sha256(source).hexdigest())
        self.addCleanup(os.close, fd)

        status = self.execute_fixture(
            fd, digest,
            collector_args=["--output-parent", str(self.root), "--candidate-dir", str(candidate)],
            record_stream=io.StringIO(),
        )

        self.assertEqual(status, 0)
        self.assertRegex(source_report.read_text().strip(), r"^/proc/self/fd/[0-9]+$")
        self.assertEqual(digest, source_hash.read_text().split()[0])
        self.assertEqual(str(candidate), (self.root / "candidate-path.txt").read_text().strip())

    def test_candidate_directory_rejects_missing_symlink_and_expected_path_mismatch(self):
        missing = self.root / "missing"
        with self.assertRaisesRegex(runner.RunnerError, "cannot be opened safely"):
            runner._verify_candidate_dir(str(missing))
        target = self.root / "real-candidate"
        target.mkdir()
        link = self.root / "candidate-link"
        link.symlink_to(target, target_is_directory=True)
        with self.assertRaisesRegex(runner.RunnerError, "symlink"):
            runner._verify_candidate_dir(str(link))
        with self.assertRaisesRegex(runner.RunnerError, "verified checkout"):
            runner._verify_candidate_dir(str(target), expected_path=str(self.root / "other"))

    def test_malformed_collector_interface_never_starts_bash(self):
        p = self.fixture()
        fd, digest = runner.snapshot_collector(p, hashlib.sha256(p.read_bytes()).hexdigest())
        self.addCleanup(os.close, fd)
        with mock.patch.object(runner.subprocess, "Popen") as popen:
            with self.assertRaisesRegex(runner.RunnerError, "requires --output-parent and --candidate-dir"):
                runner.execute_snapshot(fd, collector_args=["--output-parent", str(self.root)])
        popen.assert_not_called()

    def test_execute_cli_requires_candidate_before_collector_read(self):
        with mock.patch.dict(runner.__dict__, {"__alpha_m1_verified_runner_sha256__": "a" * 64}), \
             mock.patch.object(runner, "snapshot_collector") as snapshot, \
             contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as raised:
                runner.main(["--execute", "--output-parent", str(self.root)])
        self.assertEqual(2, raised.exception.code)
        snapshot.assert_not_called()

    def test_execute_cli_binds_candidate_to_bootstrap_checkout_before_snapshot(self):
        checkout = self.root / "reviewed checkout"
        candidate = checkout / "qualification"
        candidate.mkdir(parents=True)
        output_parent = self.root / "output parent"
        output_parent.mkdir()
        fake_fd = os.memfd_create("candidate-cli-test", os.MFD_CLOEXEC)
        os.write(fake_fd, b"synthetic collector")
        record = io.StringIO()
        with mock.patch.dict(runner.__dict__, {
                 "__alpha_m1_verified_runner_sha256__": "a" * 64,
                 "__alpha_m1_verified_repo_root__": str(checkout),
             }), \
             mock.patch.object(runner, "snapshot_collector", return_value=(fake_fd, "a" * 64)) as snapshot, \
             mock.patch.object(runner, "execute_snapshot", return_value=23) as execute, \
             contextlib.redirect_stdout(io.StringIO()):
            result = runner.main([
                "--execute", "--output-parent", str(output_parent),
                "--candidate-dir", str(candidate),
            ])
        self.assertEqual(23, result)
        snapshot.assert_called_once()
        self.assertEqual(
            ["--output-parent", str(output_parent), "--candidate-dir", str(candidate)],
            execute.call_args.kwargs["collector_args"],
        )
        execute.assert_called_once()
        with self.assertRaises(OSError):
            os.fstat(fake_fd)

        wrong_candidate = self.root / "other qualification"
        wrong_candidate.mkdir()
        with mock.patch.dict(runner.__dict__, {
                 "__alpha_m1_verified_runner_sha256__": "a" * 64,
                 "__alpha_m1_verified_repo_root__": str(checkout),
             }), \
             mock.patch.object(runner, "snapshot_collector") as rejected_snapshot, \
             contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaisesRegex(runner.RunnerError, "verified checkout"):
                runner.main([
                    "--execute", "--output-parent", str(output_parent),
                    "--candidate-dir", str(wrong_candidate),
                ])
        rejected_snapshot.assert_not_called()

    def test_execution_record_write_failure_is_reported_after_child_status(self):
        p = self.fixture(b"printf synthetic-complete\n")
        fd, digest = runner.snapshot_collector(p, hashlib.sha256(p.read_bytes()).hexdigest())
        self.addCleanup(os.close, fd)

        class FailingRecord:
            def write(self, _value):
                return 0

            def flush(self):
                return None

        with self.assertRaisesRegex(runner.RunnerError, "partial record"):
            self.execute_fixture(fd, digest, collector_args=self.collector_args(),
                                 record_stream=FailingRecord())

    def test_concurrent_mutation_during_snapshot_is_rejected(self):
        p = self.fixture(b"A" * 32_768)
        expected = hashlib.sha256(p.read_bytes()).hexdigest()
        real_read = runner.os.read
        mutated = False

        def changing_read(fd, count):
            nonlocal mutated
            data = real_read(fd, min(count, 4096))
            if not mutated and data:
                mutated = True
                with p.open("r+b") as f:
                    f.seek(8192)
                    f.write(b"Z" * 32)
            return data

        runner.os.read = changing_read
        try:
            with self.assertRaises(runner.RunnerError):
                runner.snapshot_collector(p, expected)
        finally:
            runner.os.read = real_read

    def test_interrupted_collector_status_is_recorded(self):
        pid_file = self.root / "child.pid"
        p = self.fixture(f"sleep 30 &\necho $! > {pid_file}\nwait\n".encode())
        digest = hashlib.sha256(p.read_bytes()).hexdigest()
        fd, _ = runner.snapshot_collector(p, digest)
        self.addCleanup(os.close, fd)
        record = io.StringIO()
        spawned = threading.Event()
        real_popen = runner.subprocess.Popen

        def observed_popen(*args, **kwargs):
            child = real_popen(*args, **kwargs)
            spawned.set()
            return child

        runner.subprocess.Popen = observed_popen
        timer = threading.Thread(target=lambda: (spawned.wait(3), time.sleep(0.1),
                                                  os.kill(os.getpid(), signal.SIGTERM)))
        timer.start()
        try:
            status = self.execute_fixture(fd, digest, collector_args=self.collector_args(),
                                          record_stream=record)
        finally:
            runner.subprocess.Popen = real_popen
        timer.join()
        self.assertEqual(status, -signal.SIGTERM)
        self.assertIn('"interrupt_signal": 15', record.getvalue())
        child_pid = int(pid_file.read_text())
        for _ in range(100):
            try:
                state = Path(f"/proc/{child_pid}/stat").read_text().split()[2]
            except FileNotFoundError:
                break
            if state == "Z":
                break
            time.sleep(0.01)
        else:
            self.fail("collector subprocess survived runner interruption")

    def test_nonzero_failure_and_output_integrity(self):
        marker = self.root / "marker"
        p = self.fixture(f"printf 'exact-output' > {marker}\nexit 23\n".encode())
        fd, _ = runner.snapshot_collector(p, hashlib.sha256(p.read_bytes()).hexdigest())
        self.addCleanup(os.close, fd)
        record = io.StringIO()
        digest = hashlib.sha256(p.read_bytes()).hexdigest()
        status = self.execute_fixture(fd, digest, collector_args=self.collector_args(),
                                      record_stream=record)
        self.assertEqual(status, 23)
        self.assertEqual(marker.read_bytes(), b"exact-output")
        self.assertIn('"exit_status": 23', record.getvalue())

    def test_open_failure_is_conservative(self):
        with self.assertRaises(runner.RunnerError):
            runner.snapshot_collector(self.root)

    def test_read_failure_is_conservative(self):
        p = self.fixture()
        actual_read = runner.os.read
        runner.os.read = lambda *_args: (_ for _ in ()).throw(OSError("injected read failure"))
        try:
            with self.assertRaisesRegex(runner.RunnerError, "read/snapshot failed"):
                runner.snapshot_collector(p)
        finally:
            runner.os.read = actual_read

    def test_bash_env_is_not_inherited(self):
        marker = self.root / "env-marker"
        env_file = self.root / "bash-env"
        env_file.write_text(f"printf injected > {marker}\n")
        script = self.fixture(b"printf safe\n")
        digest = hashlib.sha256(script.read_bytes()).hexdigest()
        fd, _ = runner.snapshot_collector(script, digest)
        self.addCleanup(os.close, fd)
        old = os.environ.get("BASH_ENV")
        old_func = os.environ.get("BASH_FUNC_printf%%")
        os.environ["BASH_ENV"] = str(env_file)
        os.environ["BASH_FUNC_printf%%"] = f"() {{ printf injected > {marker}; }}"
        try:
            status = self.execute_fixture(fd, digest, collector_args=self.collector_args(),
                                          record_stream=io.StringIO())
        finally:
            if old is None:
                os.environ.pop("BASH_ENV", None)
            else:
                os.environ["BASH_ENV"] = old
            if old_func is None:
                os.environ.pop("BASH_FUNC_printf%%", None)
            else:
                os.environ["BASH_FUNC_printf%%"] = old_func
        self.assertEqual(status, 0)
        self.assertFalse(marker.exists())

    def test_fixed_collector_verification_only_cli(self):
        # This test must not open or snapshot the real repository collector.
        fake_fd = os.memfd_create("synthetic-verification-only", os.MFD_CLOEXEC)
        os.write(fake_fd, b"synthetic collector bytes\n")
        fake_digest = runner.EXPECTED_SHA256
        out = io.StringIO()
        with mock.patch.dict(runner.__dict__, {"__alpha_m1_verified_runner_sha256__": fake_digest}), \
             mock.patch.object(runner, "snapshot_collector", return_value=(fake_fd, fake_digest)) as snapshot:
            with contextlib.redirect_stdout(out):
                self.assertEqual(runner.main([]), 0)
        snapshot.assert_called_once()
        self.assertEqual(snapshot.call_args.args[0].as_posix(),
                         (Path(__file__).resolve().parents[1] / runner.COLLECTOR_RELATIVE).as_posix())
        with self.assertRaises(OSError):
            os.fstat(fake_fd)  # main closed its owned descriptor
        self.assertIn(f"verified_sha256={fake_digest}", out.getvalue())
        self.assertIn("verification_only=true", out.getvalue())

    def test_main_api_rejects_missing_bootstrap_marker_before_collector_access(self):
        with mock.patch.object(runner, "snapshot_collector") as snapshot, \
             contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(runner.main([]), 70)
        snapshot.assert_not_called()

    def test_arbitrary_collector_arguments_rejected(self):
        p = self.fixture()
        fd, _ = runner.snapshot_collector(p, hashlib.sha256(p.read_bytes()).hexdigest())
        self.addCleanup(os.close, fd)
        with self.assertRaisesRegex(runner.RunnerError, "fixed collector interface"):
            runner.execute_snapshot(fd, collector_args=["/tmp/attacker.sh"])


if __name__ == "__main__":
    unittest.main()
