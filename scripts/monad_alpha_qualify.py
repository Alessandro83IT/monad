#!/usr/bin/env python3
"""Unprivileged client for the separately installed alpha.1 qualification runner.

This checkout is never executed with privilege. The protected runner performs
the identity/isolation checks and, for --run, executes a protected harness.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import stat
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
STAGE = ROOT / ".codex-alpha-release-candidate-20261031/stage"
PRIVILEGED_RUNNER = Path("/usr/libexec/monad-alpha-qualify-runner")
EXPECTED_VERSION = "Monad Package Manager 0.1.0-alpha.1"
RUN_AUTH = "AUTHORIZE-MONAD-ALPHA-1-E2E-ON-CLONE"
# This digest is the locally measured staged artifact documented in
# docs/development/monad-0.1.0-alpha.1-artifact-reconciliation-20261031.md.
# It is a candidate identity check, not a release attestation or security pin.
EXPECTED_SHA256 = {
    "bin/mpkg": "bd20e79efe91a914a811f06eb3c3cfdb157ff7e525c7854415c89732b24e0e8f",
    "lib/libathena_core.so": "2d23dfcabc06a965a068f716be9fda26b465226f675c23eab4328061271ca2be",
}
FORBIDDEN_ENV = ("LD_PRELOAD", "LD_LIBRARY_PATH", "PYTHONPATH", "BASH_ENV", "ENV")


class Blocked(RuntimeError):
    pass


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise Blocked(message)


def check_privileged_runner(area: Path, operation: str) -> dict:
    """Ask the fixed protected runner; do not derive identity from CLI claims."""
    require(PRIVILEGED_RUNNER.is_absolute(), "privileged runner path must be absolute")
    cursor = Path("/")
    for component in PRIVILEGED_RUNNER.parts[1:-1]:
        cursor /= component
        parent_stat = cursor.lstat()
        require(stat.S_ISDIR(parent_stat.st_mode) and parent_stat.st_uid == 0
                and not (parent_stat.st_mode & 0o022),
                "protected qualification runner has an unsafe parent directory")
    require(not PRIVILEGED_RUNNER.is_symlink(), "protected qualification runner is a symlink")
    try:
        st = PRIVILEGED_RUNNER.lstat()
    except OSError as error:
        raise Blocked("protected qualification runner is not installed") from error
    require(stat.S_ISREG(st.st_mode) and st.st_uid == 0 and st.st_nlink == 1
            and stat.S_IMODE(st.st_mode) == 0o755,
            "protected qualification runner ownership/mode/type is invalid")
    result = subprocess.run([str(PRIVILEGED_RUNNER), "--operation", operation,
                             "--area", str(area)], text=True, capture_output=True,
                           check=False, timeout=1200,
                           env={"PATH": "/usr/bin:/bin", "LC_ALL": "C"},
                           stdin=subprocess.DEVNULL)
    require(result.returncode == 0,
            "protected runner rejected the request: " + result.stderr.strip())
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise Blocked("protected runner returned invalid JSON") from error
    require(payload.get("status") == "PASS", "protected runner did not report PASS")
    return payload


def check_filesystem() -> dict:
    result = subprocess.run(["/usr/bin/findmnt", "-n", "-o", "TARGET,SOURCE,FSTYPE,OPTIONS",
                            "-T", "/var/tmp"], text=True, capture_output=True,
                           check=False, timeout=10)
    require(result.returncode == 0, "cannot identify the /var/tmp filesystem")
    fields = result.stdout.strip().split()
    require(len(fields) == 4, "unexpected findmnt output for /var/tmp")
    target, source, fstype, raw_options = fields
    options = set(raw_options.split(","))
    require(fstype == "ext4" and source.startswith("/dev/"),
            f"/var/tmp must be on a local ext4 filesystem (found {source} {fstype})")
    require("rw" in options and "ro" not in options,
            "/var/tmp filesystem is not mounted read-write")
    return {"target": target, "source": source, "fstype": fstype,
            "options": sorted(options), "xattrs": "ext4 native support"}


def check_stage() -> dict:
    require(STAGE.is_dir() and not STAGE.is_symlink(), "staging directory missing or symlink")
    for directory in (STAGE, STAGE / "bin", STAGE / "lib"):
        st = directory.lstat()
        require(stat.S_ISDIR(st.st_mode) and st.st_uid == os.getuid()
                and not (st.st_mode & 0o022),
                f"staging directory must be caller-owned and not group/world writable: {directory}")
    hashes = {}
    for relative, expected in EXPECTED_SHA256.items():
        path = STAGE / relative
        require(path.is_file() and not path.is_symlink(), f"staged artifact invalid: {relative}")
        st = path.lstat()
        require(st.st_uid == os.getuid() and st.st_nlink == 1 and not (st.st_mode & 0o022),
                f"staged artifact ownership/link count/mode invalid: {relative}")
        actual = digest(path)
        hashes[relative] = actual
        require(actual == expected,
                f"staged artifact SHA-256 mismatch for {relative}: {actual}")
    binary = STAGE / "bin/mpkg"
    require(binary.stat().st_mode & 0o111, "staged mpkg is not executable")
    env = {"PATH": "/usr/bin:/bin", "LC_ALL": "C"}
    version = subprocess.run([str(binary), "--version"], cwd=STAGE,
                             env=env, text=True, capture_output=True,
                             check=False, timeout=15)
    require(version.returncode == 0 and version.stdout.strip() == EXPECTED_VERSION,
            "staged mpkg version does not match 0.1.0-alpha.1")
    return {"stage": str(STAGE), "sha256": hashes, "version": version.stdout.strip()}


def validate_area(path: Path) -> Path:
    requested = path
    require(requested.is_absolute() and ".." not in requested.parts,
            "area must be an absolute path without traversal")
    cur = Path("/")
    for part in requested.parts[1:]:
        cur /= part
        require(not cur.is_symlink(), "area path contains a symlink")
    require(requested.parent == Path("/var/tmp"), "area must be a direct child of /var/tmp")
    st = requested.lstat()
    require(stat.S_ISDIR(st.st_mode) and st.st_uid == os.getuid()
            and stat.S_IMODE(st.st_mode) == 0o700,
            "area must be a caller-owned real directory with mode 0700")
    require(not any(requested.iterdir()), "area must be empty; never reuse evidence directories")
    return requested


def new_area() -> Path:
    for _ in range(10):
        path = Path("/var/tmp") / f"monad-alpha-e2e-{time.strftime('%Y%m%dT%H%M%SZ', time.gmtime())}-{secrets.token_hex(5)}"
        try:
            path.mkdir(mode=0o700)
            os.chmod(path, 0o700)
            return validate_area(path)
        except FileExistsError:
            continue
    raise Blocked("could not allocate a unique evidence directory")


def preflight() -> dict:
    require(os.geteuid() != 0, "launcher must run as an ordinary user; no root/sudo")
    require(os.environ.get("PATH") == "/usr/bin:/bin",
            "PATH must be exactly /usr/bin:/bin")
    for name in FORBIDDEN_ENV:
        require(not os.environ.get(name), f"unsafe environment override is set: {name}")
    checks = {"filesystem": check_filesystem, "staging": check_stage}
    result, errors = {}, []
    for label, check in checks.items():
        try:
            result[label] = check()
        except (Blocked, OSError, subprocess.SubprocessError) as error:
            result[label] = {"status": "BLOCKED", "error": str(error)}
            errors.append(f"{label}: {error}")
    if errors:
        raise Blocked("; ".join(errors))
    return result


def save_json(path: Path, value: dict) -> None:
    payload = (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, "wb") as stream:
        stream.write(payload)
        stream.flush()
        os.fsync(stream.fileno())


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="preflight only; never run the E2E harness")
    mode.add_argument("--run", action="store_true", help="run E2E after all preflight gates pass")
    ap.add_argument("--authorize-e2e", help="separate explicit authorization token required by --run")
    ap.add_argument("--area", type=Path, help="new empty mode-0700 direct child of /var/tmp")
    args = ap.parse_args(argv)
    try:
        require(not args.run or args.authorize_e2e == RUN_AUTH,
                "--run requires --authorize-e2e AUTHORIZE-MONAD-ALPHA-1-E2E-ON-CLONE")
        require(not args.check or not args.authorize_e2e,
                "run authorization and clone confirmation options are valid only with --run")
        area = validate_area(args.area) if args.area else new_area()
        record = preflight()
        privileged = check_privileged_runner(area, "preflight" if args.check else "run")
        record.update({"area": str(area), "launcher": str(Path(__file__).resolve()),
                       "harness_sha256": privileged.get("harness_sha256"),
                       "privileged_runner": privileged, "mode": "check" if args.check else "run"})
        if args.check:
            save_json(area / "preflight.json", record)
            save_json(area / "preflight.sha256.json", {
                "preflight.json": digest(area / "preflight.json"),
                "harness_sha256": record["harness_sha256"],
                "staged_sha256": record["staging"]["sha256"]})
            print(json.dumps({"status": "CHECK_PASS", "area": str(area),
                              "evidence": str(area / "preflight.json")}, sort_keys=True))
            return 0
        # The protected runner owns run receipts and invokes only its protected harness.
        save_json(area / "preflight.json", record)
        print(json.dumps({"status": "PASS", "area": str(area),
                          "result": privileged}, sort_keys=True))
        return 0
    except (Blocked, OSError, subprocess.SubprocessError) as error:
        print(json.dumps({"status": "BLOCKED", "error": str(error)}, sort_keys=True), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
