"""Fixed-path, read-only host operations. Never evaluate CMake or client commands."""

from __future__ import annotations

import hashlib
import json
import os
import pathlib
import re
import selectors
import secrets
import socket
import stat
import subprocess
import time
from typing import Any

from .protocol import ProtocolError

ROOT = pathlib.Path("/")
MAX_HASH_FILE = 128 * 1024 * 1024
MAX_FIXTURE_ENTRIES = 128
MAX_CTEST_FILE = 2 * 1024 * 1024
OPERATION_TIMEOUT = 7.0
ALLOWED_EXTERNAL_TEST_EXECUTABLES = frozenset({"/usr/bin/python3"})
EXPECTED_CANDIDATE_HASHES = {
    "runner": "2290b78c55c72489c0e7dc3bb1c1fedbc573e6bc605e690b6afd3a8ab326b24c",
    "support": "86be38660154161b6f71063567241fed3fbe1ef270d04a73b7c00a69126a9d28",
    "canary": "0c2567320d3f5c461d0c9a4a19beaf62b83615c511c28e699810bbdfa49f3107",
    "sudoers": "263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18",
}
ARTIFACTS = {
    "runner": pathlib.Path("/usr/local/sbin/athena-alpha-m1-runner"),
    "support": pathlib.Path("/usr/local/sbin/gateway_support.py"),
    "sudoers": pathlib.Path("/etc/sudoers.d/athena-alpha-m1"),
    "canary_stage": pathlib.Path("/var/lib/monad-local-broker/artifacts/athena-alpha-m1-systemd-canary.py"),
    "build_dir": pathlib.Path("/opt/athena-qualification/build"),
    "canary_receipt": pathlib.Path("/var/lib/athena-alpha-m1/systemd-canary.json"),
}
FIXTURES = {
    "alpha_m1_ext4": pathlib.Path("/var/lib/athena-alpha-m1/ext4-fixture"),
    "d6_residual": pathlib.Path("/var/tmp/athena-d67-ext4"),
}


def _safe_open(path: pathlib.Path, *, directory: bool = False) -> int:
    """Open a fixed absolute path by walking from / without following symlinks."""
    if not path.is_absolute() or ".." in path.parts or "." in path.parts:
        raise ProtocolError("INVALID_REQUEST", "unsafe configured path")
    fd = os.open("/", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        parts = path.parts[1:]
        if not parts:
            return fd
        for index, part in enumerate(parts):
            last = index == len(parts) - 1
            flags = os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK
            if not last or directory:
                flags |= os.O_DIRECTORY
            child = os.open(part, flags, dir_fd=fd)
            os.close(fd)
            fd = child
            st = os.fstat(fd)
            if last and directory and not stat.S_ISDIR(st.st_mode):
                raise ProtocolError("NOT_DIRECTORY", "expected directory")
            if last and not directory and not (stat.S_ISREG(st.st_mode) or stat.S_ISDIR(st.st_mode)):
                raise ProtocolError("NOT_REGULAR_FILE", "unexpected file type")
        return fd
    except ProtocolError:
        os.close(fd)
        raise
    except OSError as exc:
        os.close(fd)
        if exc.errno in (40, 20):  # ELOOP / ENOTDIR
            raise ProtocolError("SYMLINK", "path component is not a real directory or file") from exc
        if exc.errno in (2, 13):
            raise ProtocolError("NOT_FOUND" if exc.errno == 2 else "ACCESS_DENIED", "path unavailable") from exc
        raise ProtocolError("UNAVAILABLE", "path could not be inspected") from exc


def _hash_path(path: pathlib.Path, candidate_key: str | None = None) -> dict[str, Any]:
    fd = _safe_open(path)
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode):
            raise ProtocolError("NOT_REGULAR_FILE", "hash target is not a regular file")
        if st.st_size > MAX_HASH_FILE:
            raise ProtocolError("OUTPUT_TOO_LARGE", "hash target exceeds size limit")
        digest = hashlib.sha256()
        total = 0
        deadline = time.monotonic() + OPERATION_TIMEOUT
        while True:
            if time.monotonic() > deadline:
                raise ProtocolError("TIMEOUT", "artifact hash exceeded operation deadline")
            block = os.read(fd, 1024 * 1024)
            if not block:
                break
            total += len(block)
            if total > MAX_HASH_FILE:
                raise ProtocolError("OUTPUT_TOO_LARGE", "hash target grew beyond size limit")
            digest.update(block)
        key = {"canary_stage": "canary"}.get(candidate_key, candidate_key or path.name)
        expected = EXPECTED_CANDIDATE_HASHES.get(key)
        return {"sha256": digest.hexdigest(), "size": total,
                "matches_candidate": digest.hexdigest() == expected if expected else None}
    finally:
        os.close(fd)


def _metadata(path: pathlib.Path) -> dict[str, Any]:
    fd = _safe_open(path, directory=path.name in ("build", "ext4-fixture", "athena-d67-ext4"))
    try:
        st = os.fstat(fd)
        return {"type": "directory" if stat.S_ISDIR(st.st_mode) else "regular",
                "mode": stat.S_IMODE(st.st_mode), "uid": st.st_uid, "gid": st.st_gid,
                "size": st.st_size, "device": st.st_dev, "inode": st.st_ino,
                "nlink": st.st_nlink}
    finally:
        os.close(fd)


def _run_fixed(argv: list[str], timeout: float = 2.0, output_limit: int = 16 * 1024) -> tuple[int, str]:
    """Run a fixed utility while capping pipe memory and killing on timeout/overflow."""
    process = None
    selector = selectors.DefaultSelector()
    try:
        process = subprocess.Popen(argv, shell=False, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, close_fds=True,
                                   env={"PATH": "/usr/bin:/bin", "LANG": "C",
                                        "LC_ALL": "C", "SYSTEMD_COLORS": "0"})
        assert process.stdout is not None and process.stderr is not None
        for stream, label in ((process.stdout, "stdout"), (process.stderr, "stderr")):
            os.set_blocking(stream.fileno(), False)
            selector.register(stream, selectors.EVENT_READ, label)
        deadline = time.monotonic() + timeout
        output = {"stdout": bytearray(), "stderr": bytearray()}
        while selector.get_map():
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ProtocolError("TIMEOUT", "fixed system query timed out")
            for key, _ in selector.select(remaining):
                block = os.read(key.fileobj.fileno(), min(4096, output_limit + 1))
                if not block:
                    selector.unregister(key.fileobj)
                    continue
                target = output[key.data]
                target.extend(block)
                if len(target) > output_limit:
                    raise ProtocolError("OUTPUT_TOO_LARGE", "system query output exceeded limit")
        returncode = process.wait(timeout=max(0.01, deadline - time.monotonic()))
        return returncode, output["stdout"].decode("utf-8", "replace").strip()
    except ProtocolError:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait()
        raise
    except OSError as exc:
        raise ProtocolError("UNAVAILABLE", "fixed system utility is unavailable") from exc
    except subprocess.TimeoutExpired as exc:
        if process is not None and process.poll() is None:
            process.kill(); process.wait()
        raise ProtocolError("TIMEOUT", "fixed system query timed out") from exc
    finally:
        selector.close()
        if process is not None:
            if process.stdout:
                process.stdout.close()
            if process.stderr:
                process.stderr.close()


def _mount_info(path: pathlib.Path) -> dict[str, Any]:
    # Validate exact fixture directory first; do not resolve client input.
    fd = _safe_open(path, directory=True)
    os.close(fd)
    target = str(path)
    best: dict[str, Any] | None = None
    try:
        # systemd hardening gives this service a private mount namespace. PID 1
        # remains the VM's init namespace, so report that view explicitly.
        with open("/proc/1/mountinfo", "r", encoding="utf-8") as stream:
            for line in stream:
                left, sep, right = line.rstrip("\n").partition(" - ")
                if not sep:
                    continue
                fields = left.split()
                after = right.split()
                if len(fields) < 6 or len(after) < 3:
                    continue
                mountpoint = re.sub(r"\\([0-7]{3})", lambda m: chr(int(m.group(1), 8)), fields[4])
                if target == mountpoint or target.startswith(mountpoint.rstrip("/") + "/"):
                    if best is None or len(mountpoint) > len(best["target"]):
                        best = {"target": mountpoint, "filesystem": after[0], "source": after[1],
                                "mount_options": fields[5].split(","), "super_options": after[2].split(",")}
    except OSError as exc:
        raise ProtocolError("UNAVAILABLE", "mount table unavailable") from exc
    if best is None:
        raise ProtocolError("NOT_FOUND", "no mount covers the fixture")
    try:
        namespace = os.readlink("/proc/1/ns/mnt")
    except OSError as exc:
        raise ProtocolError("UNAVAILABLE", "VM init mount namespace is unavailable") from exc
    return {**best, "view": "pid1_mount_namespace", "mount_namespace": namespace}


def _fixture_inventory(path: pathlib.Path) -> dict[str, Any]:
    fd = _safe_open(path, directory=True)
    try:
        deadline = time.monotonic() + OPERATION_TIMEOUT
        with os.scandir(fd) as iterator:
            entries = []
            for entry in iterator:
                if time.monotonic() > deadline:
                    raise ProtocolError("TIMEOUT", "fixture inventory exceeded operation deadline")
                if len(entries) >= MAX_FIXTURE_ENTRIES:
                    raise ProtocolError("OUTPUT_TOO_LARGE", "fixture contains too many entries")
                if len(entry.name.encode("utf-8", "surrogateescape")) > 255:
                    raise ProtocolError("OUTPUT_TOO_LARGE", "fixture entry name too long")
                st = entry.stat(follow_symlinks=False)
                kind = ("directory" if stat.S_ISDIR(st.st_mode) else "regular" if stat.S_ISREG(st.st_mode)
                        else "symlink" if stat.S_ISLNK(st.st_mode) else "other")
                entries.append({"name": entry.name, "type": kind, "mode": stat.S_IMODE(st.st_mode),
                                "uid": st.st_uid, "gid": st.st_gid, "size": st.st_size})
        return {"count": len(entries), "entries": sorted(entries, key=lambda e: e["name"])}
    except OSError as exc:
        raise ProtocolError("ACCESS_DENIED", "fixture inventory unavailable") from exc
    finally:
        os.close(fd)


def _cmake_args(line: str) -> tuple[str, list[str]] | None:
    """Parse one generated CMake command line without evaluating it."""
    match = re.fullmatch(r"\s*([A-Za-z_][A-Za-z0-9_]*)\((.*)\)\s*", line)
    if not match:
        return None
    name, body = match.groups()
    args: list[str] = []
    i = 0
    while i < len(body):
        while i < len(body) and body[i].isspace():
            i += 1
        if i >= len(body):
            break
        if body[i] == '"':
            i += 1
            value = []
            while i < len(body):
                if body[i] == "\\" and i + 1 < len(body):
                    value.append(body[i + 1]); i += 2
                elif body[i] == '"':
                    i += 1; break
                else:
                    value.append(body[i]); i += 1
            else:
                return None
            args.append("".join(value))
        elif body[i] == "[":
            bracket = re.match(r"\[(=*)\[", body[i:])
            if not bracket:
                return None
            eq = bracket.group(1); start = i + len(bracket.group(0)); end_token = "]" + eq + "]"
            end = body.find(end_token, start)
            if end < 0:
                return None
            args.append(body[start:end]); i = end + len(end_token)
        else:
            start = i
            while i < len(body) and not body[i].isspace():
                i += 1
            args.append(body[start:i])
    return name, args


def _build_inventory() -> dict[str, Any]:
    root = ARTIFACTS["build_dir"]
    base_fd = _safe_open(root, directory=True)
    os.close(base_fd)
    testfile = root / "CTestTestfile.cmake"
    fd = _safe_open(testfile)
    try:
        st = os.fstat(fd)
        if st.st_size > MAX_CTEST_FILE:
            raise ProtocolError("OUTPUT_TOO_LARGE", "CTest inventory file exceeds size limit")
        chunks = []
        while block := os.read(fd, 65536):
            chunks.append(block)
            if sum(map(len, chunks)) > MAX_CTEST_FILE:
                raise ProtocolError("OUTPUT_TOO_LARGE", "CTest inventory file exceeds size limit")
        text = b"".join(chunks).decode("utf-8", "strict")
    except UnicodeError as exc:
        raise ProtocolError("PROTOCOL_ERROR", "CTest inventory is not UTF-8") from exc
    finally:
        os.close(fd)
    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
    tests: dict[str, dict[str, Any]] = {}
    deadline = time.monotonic() + OPERATION_TIMEOUT
    for line in text.splitlines():
        if time.monotonic() > deadline:
            raise ProtocolError("TIMEOUT", "CTest inventory exceeded operation deadline")
        call = _cmake_args(line)
        if call is None:
            if line.strip().startswith(("add_test(", "set_tests_properties(", "subdirs(")):
                raise ProtocolError("UNAVAILABLE", "unsupported generated CTest syntax")
            continue
        command, args = call
        if command == "add_test":
            if len(args) < 2:
                raise ProtocolError("PROTOCOL_ERROR", "malformed generated add_test")
            # CMake modern signature is add_test(name command [args...]); old form is
            # add_test(name command [args...]) too in generated CTestTestfile files.
            name, executable = args[0], args[1]
            if name in tests:
                raise ProtocolError("PROTOCOL_ERROR", "duplicate CTest name")
            executable_path = pathlib.Path(executable)
            executable_ok = False
            if executable_path.is_absolute():
                try:
                    relative = executable_path.relative_to(root)
                    if ".." in relative.parts:
                        relative = None
                except ValueError:
                    relative = None
                check_path = executable_path if relative is not None else None
                if str(executable_path) in ALLOWED_EXTERNAL_TEST_EXECUTABLES:
                    # Only this fixed interpreter can be reported from outside
                    # the build tree. Resolve its system-managed symlink, then
                    # still walk the canonical path without following links.
                    resolved = pathlib.Path(os.path.realpath(executable_path))
                    if re.fullmatch(r"/usr/bin/python3(?:\.[0-9]+)?", str(resolved)):
                        check_path = resolved
                if check_path is not None:
                    try:
                        efd = _safe_open(check_path)
                        est = os.fstat(efd); os.close(efd)
                        executable_ok = stat.S_ISREG(est.st_mode) and bool(est.st_mode & 0o111)
                    except ProtocolError:
                        executable_ok = False
            tests[name] = {"name": name, "labels": [], "executable": executable_path.name,
                           "executable_present": executable_ok}
        elif command == "set_tests_properties":
            if "PROPERTIES" not in args:
                continue
            split = args.index("PROPERTIES")
            names, props = args[:split], args[split + 1:]
            values = dict(zip(props[::2], props[1::2]))
            labels = values.get("LABELS", "").split(";") if "LABELS" in values else []
            for name in names:
                if name in tests:
                    tests[name]["labels"] = [label for label in labels if label]
        elif command == "subdirs":
            # Never claim a complete inventory while silently omitting generated
            # per-directory test files. Recursive CTest execution is intentionally
            # unsupported; expose an explicit fail-closed diagnostic instead.
            raise ProtocolError("UNAVAILABLE", "CTest inventory has subdirectories; complete static traversal unavailable")
    privileged = [test for test in tests.values() if "privileged" in test["labels"]]
    return {"source": "CTestTestfile.cmake (static parse; no CMake code executed)",
            "scope": "LABELS contains privileged", "sha256": digest, "count": len(privileged),
            "tests": sorted(privileged, key=lambda x: x["name"])}


def _environment_identity() -> dict[str, Any]:
    def read(path: str, limit: int = 512) -> str | None:
        try:
            fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
            try:
                return os.read(fd, limit).decode("utf-8", "replace").strip()
            finally:
                os.close(fd)
        except OSError:
            return None

    ns = {}
    for name in ("pid", "mnt", "user", "cgroup", "uts", "net"):
        try:
            ns[name] = os.readlink(f"/proc/self/ns/{name}")
        except OSError:
            ns[name] = None
    virt_rc, virt = _run_fixed(["/usr/bin/systemd-detect-virt"], timeout=1.5)
    machine_id = read("/etc/machine-id", 128)
    return {"hostname": socket.gethostname(), "boot_id": read("/proc/sys/kernel/random/boot_id", 128),
            "machine_id_sha256": hashlib.sha256(machine_id.encode()).hexdigest() if machine_id else None,
            "kernel": os.uname().release, "pid1_comm": read("/proc/1/comm", 128),
            "virt": virt if virt_rc == 0 else "unknown", "euid": os.geteuid(),
            "egid": os.getegid(), "broker_pid": os.getpid(), "namespaces": ns}


class HostOperations:
    """The complete operation allowlist. Request data never supplies paths or commands."""

    def __init__(self, expected_environment: dict[str, str] | None = None):
        self.expected_environment = expected_environment or {"virt": "oracle", "pid1_comm": "systemd"}
        self._identity: dict[str, Any] | None = None

    def environment_identity(self) -> dict[str, Any]:
        if self._identity is None:
            self._identity = _environment_identity()
        return self._identity

    def verify_environment(self) -> None:
        identity = self.environment_identity()
        if any(identity.get(key) != value for key, value in self.expected_environment.items()):
            raise ProtocolError("ENVIRONMENT_MISMATCH", "broker is not running in the configured VM environment")

    @staticmethod
    def _args(args: dict[str, Any], expected: set[str]) -> None:
        if set(args) != expected:
            raise ProtocolError("INVALID_REQUEST", "operation arguments do not match schema")

    def execute(self, operation: str, args: dict[str, Any]) -> dict[str, Any]:
        self.verify_environment()
        if operation == "environment_identity":
            self._args(args, set()); return self.environment_identity()
        if operation == "systemd_status":
            self._args(args, set())
            rc, output = _run_fixed(["/usr/bin/systemctl", "is-system-running"])
            state = output.splitlines()[0] if output else "unknown"
            # systemctl returns 1 for degraded; that is a valid diagnostic result.
            if rc not in (0, 1):
                raise ProtocolError("UNAVAILABLE", "systemd state query failed")
            return {"state": state, "systemctl_exit": rc}
        if operation == "failed_units":
            self._args(args, set())
            rc, output = _run_fixed(["/usr/bin/systemctl", "--failed", "--no-legend", "--plain", "--no-pager"])
            if rc != 0:
                raise ProtocolError("UNAVAILABLE", "failed unit query unavailable")
            units = []
            for line in output.splitlines():
                fields = line.split(None, 4)
                if len(fields) >= 4:
                    units.append({"unit": fields[0], "load": fields[1], "active": fields[2], "sub": fields[3]})
            return {"count": len(units), "units": units}
        if operation in ("artifact_metadata", "artifact_hashes"):
            self._args(args, {"artifact"})
            artifact = args["artifact"]
            if not isinstance(artifact, str) or artifact not in ARTIFACTS:
                raise ProtocolError("INVALID_REQUEST", "unknown artifact identifier")
            path = ARTIFACTS[artifact]
            if operation == "artifact_metadata":
                return {"artifact": artifact, **_metadata(path)}
            result = _hash_path(path, artifact)
            return {"artifact": artifact, **result}
        if operation in ("fixture_mount", "fixture_inventory"):
            self._args(args, {"fixture"})
            fixture = args["fixture"]
            if not isinstance(fixture, str) or fixture not in FIXTURES:
                raise ProtocolError("INVALID_REQUEST", "unknown fixture identifier")
            path = FIXTURES[fixture]
            return _mount_info(path) if operation == "fixture_mount" else _fixture_inventory(path)
        if operation == "build_inventory":
            self._args(args, set()); return _build_inventory()
        raise ProtocolError("OP_NOT_ALLOWED", "operation is not allowlisted")

