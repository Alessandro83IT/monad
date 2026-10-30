#!/usr/bin/python3
"""Second-stage bootstrap, executed only from bytes pinned by inline loader."""
from __future__ import annotations

import hashlib
import os
from dataclasses import dataclass
from pathlib import PurePosixPath
import re
import stat
import sys

MAX_RUNNER_BYTES = 1024 * 1024
EXPECTED_RUNNER_BASENAME = "alpha-m1-verified-evidence-runner.py"
EXPECTED_RUNNER_PARENT = "qualification"
DIR_FLAGS = os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW
FILE_FLAGS = os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK
SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")


class BootstrapError(RuntimeError):
    pass


@dataclass(frozen=True)
class VerifiedSource:
    """Immutable source bytes plus the identity checked before compilation."""

    source: bytes
    sha256: str
    path: str
    repo_root: str


def _canonical_components(path: str) -> list[str]:
    if not isinstance(path, str) or not path.startswith("/"):
        raise BootstrapError("runner path must be absolute")
    parts = path.split("/")[1:]
    if not parts or any(part in ("", ".", "..") for part in parts):
        raise BootstrapError("runner path must be canonical and traversal-free")
    if len(parts) < 3:
        raise BootstrapError("runner path must name a repository checkout")
    if parts[-1] != EXPECTED_RUNNER_BASENAME or parts[-2] != EXPECTED_RUNNER_PARENT:
        raise BootstrapError("runner path is not the fixed verified-evidence runner")
    return parts


def _open_runner_nofollow(path: str) -> tuple[int, int]:
    """Open every directory component and the file without following links."""
    parts = _canonical_components(path)
    dir_fd = os.open("/", DIR_FLAGS)
    file_fd = -1
    try:
        for part in parts[:-1]:
            next_fd = os.open(part, DIR_FLAGS, dir_fd=dir_fd)
            os.close(dir_fd)
            dir_fd = next_fd
        file_fd = os.open(parts[-1], FILE_FLAGS, dir_fd=dir_fd)
        opened = os.fstat(file_fd)
        if not stat.S_ISREG(opened.st_mode):
            raise BootstrapError("runner is not a regular file")
        if opened.st_nlink != 1:
            raise BootstrapError("runner must have exactly one hard link")
        if opened.st_size < 1 or opened.st_size > MAX_RUNNER_BYTES:
            raise BootstrapError("runner size is outside the accepted range")
        return dir_fd, file_fd
    except BaseException:
        if file_fd >= 0:
            os.close(file_fd)
        os.close(dir_fd)
        raise


def read_verified_source(path: str, expected_sha256: str) -> VerifiedSource:
    """Read once from a stable no-follow descriptor; reject all drift before compile."""
    if not SHA256_RE.fullmatch(expected_sha256):
        raise BootstrapError("expected runner SHA-256 must be 64 lowercase hex digits")
    dir_fd, file_fd = _open_runner_nofollow(path)
    try:
        before = os.fstat(file_fd)
        chunks: list[bytes] = []
        digest = hashlib.sha256()
        total = 0
        while True:
            try:
                block = os.read(file_fd, min(64 * 1024, MAX_RUNNER_BYTES + 1 - total))
            except InterruptedError:
                continue
            if not block:
                break
            total += len(block)
            if total > MAX_RUNNER_BYTES:
                raise BootstrapError("runner grew beyond the accepted size")
            chunks.append(block)
            digest.update(block)
        after = os.fstat(file_fd)
        before_id = (before.st_dev, before.st_ino, before.st_mode, before.st_nlink,
                     before.st_size, before.st_mtime_ns, before.st_ctime_ns)
        after_id = (after.st_dev, after.st_ino, after.st_mode, after.st_nlink,
                    after.st_size, after.st_mtime_ns, after.st_ctime_ns)
        if before_id != after_id or total != before.st_size:
            raise BootstrapError("runner changed while being read")
        actual = digest.hexdigest()
        if actual != expected_sha256:
            raise BootstrapError(f"runner SHA-256 mismatch: expected {expected_sha256}, got {actual}")
        source = b"".join(chunks)
        # Hash and compile consume the same immutable bytes object. The pathname
        # is never reopened to load code after this point.
        return VerifiedSource(source, actual, path, str(PurePosixPath(path).parent.parent))
    except OSError as exc:
        raise BootstrapError(f"runner open/read failed: {exc}") from exc
    finally:
        os.close(file_fd)
        os.close(dir_fd)


def execute_verified_source(verified: VerifiedSource, runner_args: list[str]) -> None:
    """Compile and execute exactly the previously verified immutable bytes."""
    if not isinstance(verified.source, bytes):
        raise BootstrapError("verified source is not immutable bytes")
    if hashlib.sha256(verified.source).hexdigest() != verified.sha256:
        raise BootstrapError("verified source object changed before compilation")
    code = compile(verified.source, verified.path, "exec", dont_inherit=True)
    namespace = {
        "__name__": "__main__",
        "__file__": verified.path,
        "__package__": None,
        "__cached__": None,
        "__alpha_m1_verified_runner_sha256__": verified.sha256,
        "__alpha_m1_verified_repo_root__": verified.repo_root,
    }
    previous_argv = sys.argv
    try:
        sys.argv = [verified.path, *runner_args]
        exec(code, namespace, namespace)
    finally:
        sys.argv = previous_argv


def main(argv: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    bootstrap_digest = globals().get("__alpha_m1_verified_bootstrap_sha256__")
    if len(args) < 3:
        print("ERROR: expected bootstrap digest, runner digest, and absolute runner path", file=sys.stderr)
        return 64
    expected_bootstrap, expected_runner, runner_path, *runner_args = args
    if bootstrap_digest != expected_bootstrap or not SHA256_RE.fullmatch(expected_bootstrap):
        print("ERROR: bootstrap identity was not verified by the inline loader", file=sys.stderr)
        return 70
    try:
        verified = read_verified_source(runner_path, expected_runner)
        execute_verified_source(verified, runner_args)
        return 0
    except KeyboardInterrupt:
        print("ERROR: interrupted before verified runner completed", file=sys.stderr)
        return 130
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 70


if __name__ == "__main__":
    raise SystemExit(main())
