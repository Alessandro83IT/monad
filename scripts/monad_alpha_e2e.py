#!/usr/bin/env python3
"""Explicitly authorized disposable-VM CLI install/remove exercise.

This orchestrator is intentionally inert unless --authorize is supplied. Run
only in a disposable guest with the entire guest treated as the write boundary.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tarfile

AUTH = "RUN-IN-APPROVED-DISPOSABLE-VM"
NAME = "monad-alpha-fixture"
PAYLOAD = "Monad alpha fixture payload v1\n"
FORBIDDEN = {Path("/"), Path("/bin"), Path("/boot"), Path("/dev"),
             Path("/etc"), Path("/home"), Path("/lib"), Path("/lib64"),
             Path("/opt"), Path("/proc"), Path("/root"), Path("/run"),
             Path("/sbin"), Path("/sys"), Path("/usr"), Path("/var"),
             Path("/mnt"), Path("/media")}


def fail(message):
    raise RuntimeError(message)


def require_scanner_capability():
    """Mirror the scanner's capget + initial-user-namespace gate."""
    class Header(ctypes.Structure):
        _fields_ = [("version", ctypes.c_uint32), ("pid", ctypes.c_int)]
    class Data(ctypes.Structure):
        _fields_ = [("effective", ctypes.c_uint32), ("permitted", ctypes.c_uint32),
                    ("inheritable", ctypes.c_uint32)]
    header = Header(0x20080522, 0)
    data = (Data * 2)()
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.capget(ctypes.byref(header), ctypes.byref(data)) != 0:
        fail("capget unavailable; cannot establish scanner prerequisite")
    bit = 1 << (21 % 32)  # CAP_SYS_ADMIN
    if not (data[21 // 32].effective & bit):
        fail("effective CAP_SYS_ADMIN required")
    ns = os.stat("/proc/self/ns/user")
    if ns.st_ino != 0xEFFFFFFD:
        fail("process is not in the initial user namespace; nested userns is insufficient")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--authorize", required=True)
    ap.add_argument("--mpkg", required=True, type=Path)
    ap.add_argument("--area", required=True, type=Path,
                    help="existing empty directory under /var/tmp, owned by caller")
    args = ap.parse_args()
    if args.authorize != AUTH:
        fail("authorization token mismatch")
    requested_area = args.area.absolute()
    if not requested_area.is_absolute() or ".." in requested_area.parts:
        fail("area must be an absolute path without parent traversal")
    cursor = Path("/")
    for part in requested_area.parts[1:]:
        cursor = cursor / part
        if cursor.is_symlink():
            fail("area path must not contain symlinks")
    area = requested_area.resolve(strict=True)
    binary = args.mpkg.resolve(strict=True)
    if not area.is_dir() or area == Path("/") or area in FORBIDDEN:
        fail("unsafe area")
    if not str(area).startswith("/var/tmp/"):
        fail("area must be beneath /var/tmp in the disposable guest")
    area_stat = os.lstat(area)
    if not os.path.isdir(area) or area_stat.st_uid != os.getuid() or area_stat.st_mode & 0o077:
        fail("area must be a caller-owned, mode-0700 real directory")
    if any(area.iterdir()):
        fail("area must be empty")
    require_scanner_capability()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        fail("mpkg must resolve to an executable regular file")
    if os.environ.get("PATH") != "/usr/bin:/bin":
        fail("PATH must be exactly /usr/bin:/bin")
    for key in ("LD_PRELOAD", "LD_LIBRARY_PATH", "PYTHONPATH", "BASH_ENV", "ENV"):
        if os.environ.get(key):
            fail("unsafe environment variable set: " + key)

    base = area / "case"
    root = base / "root"
    fixture = base / "fixture"
    results = area / "results.json"
    base.mkdir(mode=0o700)
    root.mkdir(mode=0o700)
    fixture.mkdir(mode=0o700)
    pkgsrc = fixture / (NAME + "-1")
    (pkgsrc / "payload").mkdir(parents=True)
    (pkgsrc / "payload" / "proof.txt").write_text(PAYLOAD, encoding="utf-8")
    (pkgsrc / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(monad_alpha_fixture LANGUAGES NONE)\n"
        "install(FILES payload/proof.txt DESTINATION share/monad-alpha-fixture)\n",
        encoding="utf-8")
    archive = fixture / (NAME + "-1.tar.gz")
    with tarfile.open(archive, "w:gz") as tf:
        tf.add(pkgsrc, arcname=NAME + "-1", recursive=True)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    pkgdir = root / "repositories/state/packages" / NAME
    pkgdir.mkdir(parents=True)
    (pkgdir / "package.toml").write_text(
        f'name = "{NAME}"\nversion = "1"\nsource = "file://{archive}"\n'
        f'sha256 = "{digest}"\nbuild_system = "cmake"\ndependencies = []\n',
        encoding="utf-8")

    log = []
    def run(label, argv, expected=0):
        if any(not str(Path(p).resolve()).startswith(str(area) + os.sep)
               for p in (root,)):
            fail("internal root escaped area")
        env = {"PATH": "/usr/bin:/bin", "HOME": str(base), "LC_ALL": "C",
               "TMPDIR": str(base), "SOURCE_DATE_EPOCH": "1793404800"}
        command = [str(binary), "--root", str(root), *argv]
        try:
            proc = subprocess.run(command, cwd=base, env=env, stdin=subprocess.DEVNULL,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  text=True, timeout=900, check=False)
        except subprocess.TimeoutExpired as exc:
            output = exc.stdout or ""
            if isinstance(output, bytes):
                output = output.decode("utf-8", errors="replace")
            log.append({"label": label, "argv": command, "exit_code": None,
                        "timed_out": True, "output": output})
            raise RuntimeError(f"{label}: timed out after 900 seconds") from exc
        log.append({"label": label, "argv": command, "exit_code": proc.returncode,
                    "output": proc.stdout})
        if proc.returncode != expected:
            fail(f"{label}: expected exit {expected}, got {proc.returncode}: {proc.stdout}")
        return proc.stdout

    try:
        if run("version", ["--version"]).strip() != "Monad Package Manager 0.1.0-alpha.1":
            fail("binary version mismatch")
        run("install", ["install", NAME])
        installed = root / "target/share/monad-alpha-fixture/proof.txt"
        if not installed.is_symlink() or installed.read_text(encoding="utf-8") != PAYLOAD:
            fail("installed payload absent, not linked, or content mismatch")
        resolved_payload = installed.resolve(strict=True)
        if not resolved_payload.is_relative_to(root / "store"):
            fail("activation symlink resolves outside the Monad store")
        store_payloads = []
        store_root = root / "store"
        if store_root.exists():
            root_device = store_root.stat().st_dev
            count = 0
            for current, dirs, files in os.walk(store_root, followlinks=False):
                current_path = Path(current)
                if current_path.stat().st_dev != root_device:
                    fail("store traversal crossed a filesystem boundary")
                dirs[:] = [d for d in dirs if not (current_path / d).is_symlink()]
                count += len(dirs) + len(files)
                if count > 100000:
                    fail("store traversal exceeded 100000 entries")
                if "proof.txt" in files:
                    store_payloads.append(current_path / "proof.txt")
        if len(store_payloads) != 1 or store_payloads[0].read_text(encoding="utf-8") != PAYLOAD:
            fail("store payload integrity check failed")
        run("generation-current-installed", ["generation", "current"])
        info = run("info", ["info", NAME])
        if NAME not in info:
            fail("info output does not identify package")
        gens = run("generation-list-installed", ["generation", "list"])
        ids = [int(x) for x in re.findall(r"(?:^|\n)\s*\*?\s*(\d+) \(", gens)]
        if len(ids) < 2:
            fail("expected initial and installed generations")
        run("switch-initial", ["generation", "switch", str(min(ids))])
        if installed.exists() or installed.is_symlink():
            fail("switch to initial generation left target payload active")
        run("switch-installed", ["generation", "switch", str(max(ids))])
        if not installed.is_symlink():
            fail("switch back did not reactivate payload")
        run("rollback", ["generation", "rollback"])
        if installed.exists() or installed.is_symlink():
            fail("rollback did not deactivate payload")
        run("switch-installed-before-remove", ["generation", "switch", str(max(ids))])
        if not installed.is_symlink():
            fail("switch back after rollback did not reactivate payload")
        run("remove", ["remove", NAME])
        if installed.exists() or installed.is_symlink():
            fail("remove left installed payload")
        run("generation-current-removed", ["generation", "current"])
        run("generation-list-removed", ["generation", "list"])
        log.append({"label": "result", "status": "PASS"})
    except Exception as exc:
        log.append({"label": "result", "status": "FAIL", "error": str(exc)})
        results.write_text(json.dumps({"version": 1, "area": str(area), "operations": log},
                                      indent=2) + "\n", encoding="utf-8")
        raise
    results.write_text(json.dumps({"version": 1, "area": str(area), "operations": log},
                                  indent=2) + "\n", encoding="utf-8")
    print(results)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(json.dumps({"version": 1, "status": "FAIL_OR_BLOCKED",
                          "error": str(error)}, sort_keys=True), file=sys.stderr)
        sys.exit(1)
