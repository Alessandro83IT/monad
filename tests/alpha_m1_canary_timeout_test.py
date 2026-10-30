#!/usr/bin/env python3
"""Deterministic timeout and evidence-retention tests for the systemd canary."""

import importlib.util
import pathlib
import stat
import subprocess
import sys
import tempfile
import threading
import time
import types
import unittest
from unittest import mock


SOURCE = pathlib.Path(__file__).resolve().parents[1] / "qualification" / "athena-alpha-m1-systemd-canary.py"
SPEC = importlib.util.spec_from_file_location("alpha_m1_canary", SOURCE)
CANARY = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = CANARY
SPEC.loader.exec_module(CANARY)


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def monotonic(self):
        return self.now

    def sleep(self, duration):
        self.now += duration


def completed(stdout="", returncode=0):
    return types.SimpleNamespace(stdout=stdout, returncode=returncode)


def status(state="inactive", pid="0", cgroup="/system.slice/test.service", unit="test.service",
           result="success"):
    return f"Id={unit}\nActiveState={state}\nMainPID={pid}\nControlGroup={cgroup}\nResult={result}\n"


class CanaryTimeoutTest(unittest.TestCase):
    def test_requires_all_three_terminal_conditions(self):
        clock = FakeClock()
        outputs = iter([
            status("inactive", "2"),
            status("active", "0"),
            status("failed", "0"),
        ])
        calls = []

        def run(command, **kwargs):
            calls.append(kwargs["timeout"])
            return completed(next(outputs))

        def events(_path):
            return "populated 0\n"

        self.assertTrue(CANARY.confirm_unit_terminated("systemctl", "test.service", run,
                                                        clock.monotonic, clock.sleep, events, 2))
        self.assertTrue(all(0 < value <= 2 for value in calls))

    def test_populated_cgroup_or_uncertain_properties_never_confirm(self):
        clock = FakeClock()
        output = status()
        result = CANARY.confirm_unit_terminated(
            "systemctl", "test.service", lambda *_a, **_k: completed(output),
            clock.monotonic, clock.sleep, lambda _path: "populated 1\n", timeout=1)
        self.assertFalse(result)
        self.assertGreaterEqual(clock.now, 1)
        self.assertLessEqual(clock.now, 1.25)

    def test_missing_or_unsafe_cgroup_fails_closed(self):
        for cgroup in ("/", "/system.slice/../escape"):
            with self.subTest(cgroup=cgroup):
                clock = FakeClock()
                output = status("failed", "0", cgroup)
                result = CANARY.confirm_unit_terminated(
                    "systemctl", "test.service", lambda *_a, **_k: completed(output),
                    clock.monotonic, clock.sleep, lambda _path: "populated 0\n", timeout=0.5)
                self.assertFalse(result)

    def test_timeout_requests_stop_then_escalates_and_preserves_markers(self):
        clock = FakeClock()
        commands = []
        stopped = False

        def run(command, **_kwargs):
            nonlocal stopped
            commands.append(command)
            if command[0] == "systemd-run":
                raise subprocess.TimeoutExpired(command, 0.1)
            if command[:2] == ["systemctl", "stop"]:
                return completed()
            if command[:2] == ["systemctl", "kill"]:
                stopped = True
                return completed()
            return completed(status("inactive", "0") if stopped else status("active", "1"))

        result = CANARY.execute_unit(["systemd-run"], "test.service", "systemctl", run,
                                     clock.monotonic, clock.sleep,
                                     lambda _path: "populated 0\n", timeout=0.1,
                                     stop_grace=0.5, kill_timeout=1)
        self.assertTrue(result.timed_out)
        self.assertTrue(result.terminated)
        self.assertTrue(result.preserve_markers)
        self.assertFalse(CANARY.cleanup_allowed(result, result.preserve_markers))
        self.assertIn(["systemctl", "stop", "--no-block", "test.service"], commands)
        self.assertIn(["systemctl", "kill", "--kill-whom=all", "--signal=SIGKILL", "test.service"], commands)
        self.assertLess(commands.index(["systemctl", "stop", "--no-block", "test.service"]),
                        commands.index(["systemctl", "kill", "--kill-whom=all", "--signal=SIGKILL", "test.service"]))

    def test_interruption_or_uncertain_termination_preserves_markers(self):
        clock = FakeClock()
        def interrupted(*_args, **_kwargs):
            raise KeyboardInterrupt
        result = CANARY.execute_unit(["systemd-run"], "test.service", "systemctl", interrupted,
                                     clock.monotonic, clock.sleep,
                                     lambda _path: "populated 0\n", stop_grace=0.5, kill_timeout=1)
        self.assertTrue(result.interrupted)
        self.assertTrue(result.preserve_markers)
        self.assertFalse(CANARY.cleanup_allowed(result, result.preserve_markers))

        uncertain = CANARY.UnitOutcome(124, False, timed_out=True)
        self.assertTrue(uncertain.preserve_markers)
        self.assertFalse(CANARY.cleanup_allowed(uncertain, uncertain.preserve_markers))
        self.assertTrue(CANARY.cleanup_allowed(CANARY.UnitOutcome(0, True)))

    def test_interrupt_during_post_client_confirmation_cannot_be_success(self):
        clock = FakeClock()
        commands = []
        show_count = 0

        def run(command, **_kwargs):
            nonlocal show_count
            commands.append(command)
            if command[0] == "systemd-run":
                return completed(returncode=0)
            if command[:2] == ["systemctl", "stop"]:
                return completed()
            if command[:2] == ["systemctl", "show"]:
                show_count += 1
                if show_count == 1:
                    raise KeyboardInterrupt
                return completed(status("inactive", "0"))
            raise AssertionError("unexpected systemctl operation")

        result = CANARY.execute_unit(["systemd-run"], "test.service", "systemctl", run,
            clock.monotonic, clock.sleep, lambda _path: "populated 0\n",
            stop_grace=0.5, kill_timeout=1)
        self.assertTrue(result.interrupted)
        self.assertTrue(result.terminated)
        self.assertEqual(result.returncode, 130)
        self.assertTrue(result.preserve_markers)
        self.assertFalse(CANARY.cleanup_allowed(result, result.preserve_markers))
        with self.assertRaisesRegex(RuntimeError, "confirmed successful termination"):
            CANARY.record_success(result)
        self.assertIn(["systemctl", "stop", "--no-block", "test.service"], commands)

    def test_absent_cgroup_requires_successful_client_completion(self):
        missing = lambda _path: (_ for _ in ()).throw(FileNotFoundError())
        for output, events in (
                (status("inactive", "0", ""), lambda _path: self.fail("empty cgroup must be rejected")),
                (status("failed", "0"), missing),
                (status("failed", "0"), lambda _path: "populated 1\n")):
            clock = FakeClock()
            self.assertFalse(CANARY.confirm_unit_terminated(
                "systemctl", "test", lambda *_a, **_k: completed(output), clock.monotonic,
                clock.sleep, events, timeout=0.5))

        for output, events in ((status("inactive", "0", ""), lambda _path: ""),
                               (status("inactive", "0"), missing)):
            clock = FakeClock()
            self.assertTrue(CANARY.confirm_unit_terminated(
                "systemctl", "test", lambda *_a, **_k: completed(output), clock.monotonic,
                clock.sleep, events, timeout=1, allow_removed_after_success=True))

        clock = FakeClock()
        self.assertFalse(CANARY.confirm_unit_terminated(
            "systemctl", "test", lambda *_a, **_k: completed(
                status("failed", "0", "", result="exit-code")), clock.monotonic,
            clock.sleep, missing, timeout=0.5, allow_removed_after_success=True))

        for wrong in (status("inactive", "0", "", "other.service"),
                      "Id=test.service\nActiveState=inactive\nMainPID=0\n",
                      "ActiveState=inactive\nMainPID=0\nControlGroup=\n"):
            clock = FakeClock()
            self.assertFalse(CANARY.confirm_unit_terminated(
                "systemctl", "test", lambda *_a, **_k: completed(wrong), clock.monotonic,
                clock.sleep, missing, timeout=0.5))

        clock = FakeClock()
        self.assertFalse(CANARY.confirm_unit_terminated(
            "systemctl", "test", lambda *_a, **_k: completed(
                status("inactive", "0", "/user.slice/test.service")), clock.monotonic,
            clock.sleep, lambda _path: "populated 0\n", timeout=0.5))

    def test_query_failures_indeterminate_properties_pid_and_cgroup_fail_closed(self):
        clock = FakeClock()
        for run in (
            lambda *_a, **_k: completed(returncode=1),
            lambda *_a, **_k: (_ for _ in ()).throw(subprocess.TimeoutExpired("systemctl", 1)),
            lambda *_a, **_k: (_ for _ in ()).throw(OSError("query failed")),
        ):
            with self.subTest(run=run):
                self.assertFalse(CANARY.confirm_unit_terminated("systemctl", "test", run,
                    clock.monotonic, clock.sleep, lambda _path: "populated 0\n", timeout=0.5))
                clock.now = 0
        for output, events in (
            ("Id=test.service\nActiveState=deactivating\nMainPID=0\nControlGroup=\n", "populated 0\n"),
            (status("inactive", "9", ""), "populated 0\n"),
            (status("inactive", "0"), "populated 1\n"),
            (status("inactive", "0"), "populated maybe\n"),
        ):
            clock.now = 0
            self.assertFalse(CANARY.confirm_unit_terminated("systemctl", "test",
                lambda *_a, **_k: completed(output), clock.monotonic, clock.sleep,
                lambda _path, data=events: data, timeout=0.5))
        clock.now = 0
        self.assertFalse(CANARY.confirm_unit_terminated("systemctl", "test",
            lambda *_a, **_k: completed(status("inactive", "0")), clock.monotonic,
            clock.sleep, lambda _path: (_ for _ in ()).throw(PermissionError()), timeout=0.5))

    def test_stop_failure_escalates_and_unknown_state_blocks_cleanup(self):
        clock = FakeClock()
        commands = []
        def run(command, **_kwargs):
            commands.append(command)
            if command[:2] == ["systemctl", "stop"]:
                raise subprocess.TimeoutExpired(command, 1)
            if command[:2] == ["systemctl", "kill"]:
                raise OSError("kill denied")
            return completed(status("active", "4"))
        terminated = CANARY.stop_and_confirm_unit("systemctl", "test", run, clock.monotonic,
            clock.sleep, lambda _path: "populated 1\n", grace_timeout=0.5, kill_timeout=0.5)
        self.assertFalse(terminated)
        self.assertIn(["systemctl", "stop", "--no-block", "test"], commands)
        self.assertIn(["systemctl", "kill", "--kill-whom=all", "--signal=SIGKILL", "test"], commands)
        self.assertFalse(CANARY.cleanup_allowed(CANARY.UnitOutcome(1, False)))

    def test_prior_receipt_archived_only_after_confirmed_success(self):
        with tempfile.TemporaryDirectory() as directory:
            receipt = pathlib.Path(directory) / "systemd-canary.json"
            receipt.write_bytes(b"old-evidence\n")
            real_fstat = CANARY.os.fstat

            def fake_fstat(fd):
                actual = real_fstat(fd)
                mode = (actual.st_mode & ~0o7777) | (0o700 if stat.S_ISDIR(actual.st_mode) else 0o600)
                return types.SimpleNamespace(st_mode=mode, st_uid=0, st_dev=actual.st_dev,
                    st_ino=actual.st_ino, st_size=actual.st_size, st_mtime_ns=actual.st_mtime_ns,
                    st_ctime_ns=actual.st_ctime_ns, st_nlink=actual.st_nlink)

            def read_text(path, *args, **kwargs):
                if str(path) == "/proc/sys/kernel/random/boot_id":
                    return "fake-boot-id\n"
                return original_read_text(path, *args, **kwargs)

            original_read_text = pathlib.Path.read_text
            with mock.patch.object(CANARY, "RECEIPT", str(receipt)), \
                    mock.patch.object(CANARY, "protected_sha256", return_value="0" * 64), \
                    mock.patch.object(CANARY.subprocess, "run", return_value=completed("systemd 257\n")), \
                    mock.patch.object(pathlib.Path, "read_text", read_text), \
                    mock.patch.object(CANARY.os, "fstat", side_effect=fake_fstat), \
                    mock.patch.object(CANARY.os, "listxattr", return_value=[]):
                uncertain = CANARY.UnitOutcome(124, False, timed_out=True)
                with self.assertRaisesRegex(RuntimeError, "confirmed successful termination"):
                    CANARY.record_success(uncertain)
                self.assertEqual(receipt.read_bytes(), b"old-evidence\n")
                self.assertEqual(list(pathlib.Path(directory).glob(".systemd-canary-archive.*")), [])

                CANARY.SYSTEMCTL = "/bin/systemctl"
                CANARY.record_success(CANARY.UnitOutcome(0, True))
                archives = list(pathlib.Path(directory).glob(".systemd-canary-archive.*.json"))
                self.assertEqual(len(archives), 1)
                self.assertEqual(archives[0].read_bytes(), b"old-evidence\n")
                self.assertIn(b'"passed":true', receipt.read_bytes())

    def test_concurrent_publishers_serialize_and_archive_each_predecessor(self):
        with tempfile.TemporaryDirectory() as directory:
            receipt = pathlib.Path(directory) / "systemd-canary.json"
            receipt.write_bytes(b"old-evidence\n")
            real_fstat = CANARY.os.fstat
            def fake_fstat(fd):
                actual = real_fstat(fd)
                mode = (actual.st_mode & ~0o7777) | (0o700 if stat.S_ISDIR(actual.st_mode) else 0o600)
                return types.SimpleNamespace(st_mode=mode, st_uid=0, st_dev=actual.st_dev,
                    st_ino=actual.st_ino, st_size=actual.st_size, st_mtime_ns=actual.st_mtime_ns,
                    st_ctime_ns=actual.st_ctime_ns, st_nlink=actual.st_nlink)

            original_read_text = pathlib.Path.read_text
            def read_text(path, *args, **kwargs):
                if str(path) == "/proc/sys/kernel/random/boot_id":
                    return "fake-boot-id\n"
                return original_read_text(path, *args, **kwargs)

            entered_archive = threading.Event()
            allow_first_archive = threading.Event()
            second_archive = threading.Event()
            second_started = threading.Event()
            archive_guard = threading.Lock()
            archive_number = 0
            errors = []
            original_archive = CANARY._archive_existing_receipt

            def controlled_archive(dir_fd, name):
                nonlocal archive_number
                with archive_guard:
                    archive_number += 1
                    current = archive_number
                if current == 1:
                    entered_archive.set()
                    if not allow_first_archive.wait(5):
                        raise TimeoutError("test did not release first publisher")
                else:
                    second_archive.set()
                return original_archive(dir_fd, name)

            clock_values = iter((1001, 1002))
            def publish(first):
                try:
                    if not first:
                        second_started.set()
                    CANARY.record_success(CANARY.UnitOutcome(0, True))
                except BaseException as exc:
                    errors.append(exc)

            with mock.patch.object(CANARY, "RECEIPT", str(receipt)), \
                    mock.patch.object(CANARY, "protected_sha256", return_value="0" * 64), \
                    mock.patch.object(CANARY.subprocess, "run", return_value=completed("systemd 257\n")), \
                    mock.patch.object(CANARY.time, "time_ns", side_effect=lambda: next(clock_values)), \
                    mock.patch.object(pathlib.Path, "read_text", read_text), \
                    mock.patch.object(CANARY.os, "fstat", side_effect=fake_fstat), \
                    mock.patch.object(CANARY.os, "listxattr", return_value=[]), \
                    mock.patch.object(CANARY, "_archive_existing_receipt", side_effect=controlled_archive):
                first = threading.Thread(target=publish, args=(True,))
                second = threading.Thread(target=publish, args=(False,))
                first.start()
                self.assertTrue(entered_archive.wait(2))
                second.start()
                self.assertTrue(second_started.wait(2))
                self.assertFalse(second_archive.wait(0.2), "second publisher entered archive while first held lock")
                allow_first_archive.set()
                first.join(5)
                second.join(5)
                self.assertFalse(first.is_alive())
                self.assertFalse(second.is_alive())
            self.assertEqual(errors, [])
            archives = list(pathlib.Path(directory).glob(".systemd-canary-archive.*.json"))
            self.assertEqual(len(archives), 2)
            archive_bytes = [path.read_bytes() for path in archives]
            self.assertIn(b"old-evidence\n", archive_bytes)
            self.assertTrue(any(b'"validated_ns":1001' in payload for payload in archive_bytes))
            self.assertIn(b'"validated_ns":1002', receipt.read_bytes())


if __name__ == "__main__":
    unittest.main()
