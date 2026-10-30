#!/usr/bin/env python3
"""Verify the pinned Alpha M1 collector and optionally run its sealed snapshot.

This runner is deliberately bound to one repository-relative collector and one
digest. It is not a general-purpose script launcher.
"""
from __future__ import annotations

import argparse
import datetime as _dt
import errno
import fcntl
import hashlib
import json
import os
from pathlib import Path
import signal
import stat
import subprocess
import sys
import time

EXPECTED_SHA256 = "ec222eae049c8469217f15c93043e82bf4bc7308d86499044aebc902550d760e"
COLLECTOR_RELATIVE = Path("qualification/alpha-m1-vm-evidence-collector.sh")
SEALS = (fcntl.F_SEAL_WRITE | fcntl.F_SEAL_GROW | fcntl.F_SEAL_SHRINK |
         fcntl.F_SEAL_SEAL)


class RunnerError(RuntimeError):
    pass


def _verify_candidate_dir(path: str, *, expected_path: str | None = None) -> str:
    """Require an absolute canonical directory reached without following links."""
    if not isinstance(path, str) or not path.startswith("/"):
        raise RunnerError("candidate directory must be absolute")
    parts = path.split("/")[1:]
    if not parts or any(part in ("", ".", "..") for part in parts):
        raise RunnerError("candidate directory must be canonical and traversal-free")
    if expected_path is not None and path != expected_path:
        raise RunnerError("candidate directory is not the verified checkout qualification directory")

    directory_fd = -1
    try:
        directory_fd = os.open("/", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW)
        for part in parts:
            next_fd = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                              dir_fd=directory_fd)
            os.close(directory_fd)
            directory_fd = next_fd
        if not stat.S_ISDIR(os.fstat(directory_fd).st_mode):
            raise RunnerError("candidate path is not a directory")
        return path
    except OSError as exc:
        if exc.errno in (errno.ELOOP, errno.ENOTDIR):
            raise RunnerError("candidate directory contains a symlink or non-directory component") from exc
        raise RunnerError(f"candidate directory cannot be opened safely: {exc}") from exc
    finally:
        if directory_fd >= 0:
            os.close(directory_fd)


def _utc_now() -> str:
    return _dt.datetime.now(_dt.timezone.utc).isoformat(timespec="seconds")


def _new_memfd() -> int:
    if not hasattr(os, "memfd_create") or not hasattr(fcntl, "F_ADD_SEALS"):
        raise RunnerError("sealed memfd support is required; refusing fallback execution")
    return os.memfd_create("alpha-m1-verified-collector", os.MFD_CLOEXEC | os.MFD_ALLOW_SEALING)


def snapshot_collector(path: Path, expected_sha256: str = EXPECTED_SHA256) -> tuple[int, str]:
    """Copy one no-follow regular file into a sealed anonymous fd, then hash it."""
    fd = -1
    snapshot_fd = -1
    try:
        fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK)
        before = os.fstat(fd)
        if not stat.S_ISREG(before.st_mode) or before.st_nlink != 1:
            raise RunnerError("collector must be a singly linked regular file")
        snapshot_fd = _new_memfd()
        digest = hashlib.sha256()
        while True:
            block = os.read(fd, 1024 * 1024)
            if not block:
                break
            digest.update(block)
            view = memoryview(block)
            while view:
                n = os.write(snapshot_fd, view)
                view = view[n:]
        after = os.fstat(fd)
        # Metadata changes during capture are suspicious. The digest remains the
        # authoritative byte check, but conservative rejection simplifies review.
        if (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (
                after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns):
            raise RunnerError("collector changed while being snapshotted")
        actual = digest.hexdigest()
        if actual != expected_sha256:
            raise RunnerError(f"collector SHA-256 mismatch: expected {expected_sha256}, got {actual}")
        fcntl.fcntl(snapshot_fd, fcntl.F_ADD_SEALS, SEALS)
        if fcntl.fcntl(snapshot_fd, fcntl.F_GET_SEALS) & SEALS != SEALS:
            raise RunnerError("could not confirm all immutable snapshot seals")
        os.close(fd)
        fd = -1
        return snapshot_fd, actual
    except OSError as exc:
        if snapshot_fd >= 0:
            os.close(snapshot_fd)
        if exc.errno == errno.ELOOP:
            raise RunnerError("collector path is a symlink") from exc
        raise RunnerError(f"collector open/read/snapshot failed: {exc}") from exc
    except BaseException:
        if snapshot_fd >= 0:
            os.close(snapshot_fd)
        raise
    finally:
        if fd >= 0:
            os.close(fd)


def _verify_sealed_snapshot(snapshot_fd: int, expected_sha256: str) -> None:
    """Refuse any descriptor that is not an immutable snapshot of the pin."""
    try:
        metadata = os.fstat(snapshot_fd)
        if not stat.S_ISREG(metadata.st_mode):
            raise RunnerError("execution descriptor is not a regular memfd")
        seals = fcntl.fcntl(snapshot_fd, fcntl.F_GET_SEALS)
        if seals & SEALS != SEALS:
            raise RunnerError("execution descriptor lacks required immutable seals")
        digest = hashlib.sha256()
        offset = 0
        while offset < metadata.st_size:
            block = os.pread(snapshot_fd, min(1024 * 1024, metadata.st_size - offset), offset)
            if not block:
                raise RunnerError("unexpected EOF in sealed execution descriptor")
            digest.update(block)
            offset += len(block)
        if digest.hexdigest() != expected_sha256:
            raise RunnerError("sealed execution descriptor digest does not match verified digest")
    except OSError as exc:
        raise RunnerError(f"sealed snapshot validation failed: {exc}") from exc


def execute_snapshot(snapshot_fd: int, *, collector_args: list[str],
                     record_stream=None, provenance: dict | None = None) -> int:
    """Execute only the sealed fd through the fixed /bin/bash interpreter."""
    if not collector_args or collector_args[0] != "--output-parent":
        raise RunnerError("fixed collector interface requires --output-parent first")
    if len(collector_args) != 4 or collector_args[2] != "--candidate-dir":
        raise RunnerError("collector requires --output-parent and --candidate-dir")
    if not os.path.isabs(collector_args[1]):
        raise RunnerError("collector output parent must be absolute")
    _verify_candidate_dir(collector_args[3])
    _verify_sealed_snapshot(snapshot_fd, EXPECTED_SHA256)
    proc_path = f"/proc/self/fd/{snapshot_fd}"
    started = _utc_now()
    status: int
    caught_signal: int | None = None
    old_handlers = {}
    try:
        safe_env = {"PATH": "/usr/bin:/bin", "LANG": "C", "LC_ALL": "C", "TZ": "UTC"}
        child = None

        def forward_signal(signum, _frame):
            nonlocal caught_signal
            caught_signal = signum
            if child is not None:
                try:
                    os.killpg(child.pid, signum)
                except ProcessLookupError:
                    pass

        for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
            old_handlers[sig] = signal.signal(sig, forward_signal)
        child = subprocess.Popen(["/bin/bash", proc_path, *collector_args], pass_fds=(snapshot_fd,),
                                 close_fds=True, env=safe_env, start_new_session=True)
        if caught_signal is not None:
            try:
                os.killpg(child.pid, caught_signal)
            except ProcessLookupError:
                pass
        status = child.wait()
    except OSError as exc:
        raise RunnerError(f"collector process could not start: {exc}") from exc
    finally:
        for sig, handler in old_handlers.items():
            signal.signal(sig, handler)
    record = {
        "schema": "alpha-m1-verified-evidence-runner/v1",
        "collector_sha256": EXPECTED_SHA256,
        "collector_source": str(COLLECTOR_RELATIVE),
        "executed_object": "sealed-anonymous-memfd-via-/bin/bash",
        "started_utc": started,
        "finished_utc": _utc_now(),
        "exit_status": status,
        "interrupt_signal": caught_signal,
        "provenance": provenance or {},
    }
    encoded = json.dumps(record, sort_keys=True) + "\n"
    if record_stream is not None:
        try:
            stream = record_stream
            written = stream.write(encoded)
            stream.flush()
            if written != len(encoded):
                raise RunnerError("execution record stream accepted only a partial record")
        except (OSError, ValueError) as exc:
            raise RunnerError("collector completed but the execution record could not be emitted") from exc
    else:
        data = memoryview(encoded.encode())
        try:
            while data:
                written = os.write(2, data)
                if written <= 0:
                    raise OSError("stderr write made no progress")
                data = data[written:]
        except OSError as exc:
            raise RunnerError("collector completed but the execution record could not be emitted") from exc
    return status


def main(argv: list[str] | None = None) -> int:
    verified_digest = globals().get("__alpha_m1_verified_runner_sha256__")
    if not isinstance(verified_digest, str) or len(verified_digest) != 64:
        print("ERROR: launch this runner through the verified-execution bootstrap", file=sys.stderr)
        return 70
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--execute", action="store_true", help="execute after pin verification (explicit authorization required)")
    parser.add_argument("--output-parent", help="existing private collector output parent")
    parser.add_argument("--candidate-dir", help="fixed collector candidate directory")
    ns = parser.parse_args(argv)
    if (ns.output_parent is None) != (not ns.execute):
        parser.error("--execute and --output-parent must be supplied together")
    if ns.candidate_dir and not ns.execute:
        parser.error("--candidate-dir is valid only with --execute")
    verified_root = globals().get("__alpha_m1_verified_repo_root__")
    # The verified-execution bootstrap supplies the root alongside the exact
    # source bytes it hashed. The fallback is retained for unit tests importing
    # this module; direct CLI entry is rejected below.
    root = Path(verified_root) if verified_root is not None else Path(__file__).resolve().parent.parent
    collector = root / COLLECTOR_RELATIVE
    candidate_dir = None
    if ns.execute:
        if ns.candidate_dir is None:
            parser.error("--execute requires an explicit --candidate-dir")
        expected_candidate = str(root / "qualification") if verified_root is not None else None
        candidate_dir = _verify_candidate_dir(ns.candidate_dir, expected_path=expected_candidate)
    snapshot_fd = -1
    try:
        snapshot_fd, digest = snapshot_collector(collector)
        print(f"verified_sha256={digest}")
        if not ns.execute:
            print("verification_only=true; collector was not executed")
            return 0
        args = ["--output-parent", ns.output_parent, "--candidate-dir", candidate_dir]
        return execute_snapshot(snapshot_fd, collector_args=args,
                                provenance={"runner": str(Path(__file__).absolute()),
                                            "uid": os.getuid(), "pid": os.getpid(),
                                            "cwd": os.getcwd()})
    except (OSError, RunnerError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 70
    finally:
        if snapshot_fd >= 0:
            os.close(snapshot_fd)


if __name__ == "__main__":
    raise SystemExit(main())
