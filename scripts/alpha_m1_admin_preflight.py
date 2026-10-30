#!/usr/bin/env python3
"""Read-only Alpha M1 administrative preflight; never invokes sudo or tests."""

from __future__ import annotations

import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tomllib

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = pathlib.Path("/opt/athena-qualification/build")
FIXTURES = (pathlib.Path("/var/lib/athena-alpha-m1/ext4-fixture"),
            pathlib.Path("/var/tmp/athena-d67-ext4"))
COMPONENTS = {
    "runner": (ROOT / "qualification/athena-alpha-m1-runner.py", "2290b78c55c72489c0e7dc3bb1c1fedbc573e6bc605e690b6afd3a8ab326b24c"),
    "gateway_support": (ROOT / "qualification/gateway_support.py", "86be38660154161b6f71063567241fed3fbe1ef270d04a73b7c00a69126a9d28"),
    "canary": (ROOT / "qualification/athena-alpha-m1-systemd-canary.py", "0c2567320d3f5c461d0c9a4a19beaf62b83615c511c28e699810bbdfa49f3107"),
    "sudoers_draft": (ROOT / "qualification/athena-alpha-m1-sudoers", "263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18"),
    "installer": (ROOT / "qualification/install-athena-alpha-m1-admin.sh", "55f13e8d1d91e149b6f24a8257fa48339f2b48e2a1b619f5ae0a5b1053d463a8"),
    "orchestrator": (ROOT / "qualification/run-alpha-m1-privileged.py", "20a87b8fc6f2f8cf55bb153b4fcc9362f937a97857e894866e0d05963d8ea83e"),
}


def item(status: str, detail: str) -> dict[str, str]:
    return {"status": status, "detail": detail}


def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    checks: dict[str, dict[str, str]] = {}
    try:
        virt = subprocess.run(["systemd-detect-virt"], text=True, capture_output=True, timeout=5)
        virt_name = virt.stdout.strip() or "none"
        checks["environment"] = item("PASS" if virt_name == "none" else "BLOCKED",
                                      f"systemd-detect-virt={virt_name}; Debian identity alone is insufficient")
    except (OSError, subprocess.SubprocessError) as exc:
        checks["environment"] = item("NOT VERIFIED", str(exc))

    codex = shutil.which("codex") or "/home/alessandro/.local/npm/bin/codex"
    try:
        version = subprocess.run([codex, "--version"], text=True, capture_output=True, timeout=10)
        cfg = tomllib.loads((ROOT / ".codex/config.toml").read_text())
        agents = sorted(p.stem for p in (ROOT / ".codex/agents").glob("*.toml"))
        valid = version.returncode == 0 and cfg.get("agents", {}).get("enabled") is True and len(agents) == 4
        checks["codex"] = item("PASS" if valid else "BLOCKED",
                               f"{version.stdout.strip()}; project agents enabled={cfg.get('agents', {}).get('enabled')}, profiles={','.join(agents)}")
    except (OSError, subprocess.SubprocessError, ValueError, KeyError) as exc:
        checks["codex"] = item("NOT VERIFIED", str(exc))

    pins_ok = True
    for name, (path, expected) in COMPONENTS.items():
        try:
            actual = sha256(path)
            status = "PASS" if actual == expected else "FAIL"
            pins_ok &= status == "PASS"
            checks[f"hash:{name}"] = item(status, f"{actual} ({path.relative_to(ROOT)})")
        except OSError as exc:
            pins_ok = False
            checks[f"hash:{name}"] = item("NOT VERIFIED", str(exc))
    try:
        installer = COMPONENTS["installer"][0].read_text()
        for label, key in (("RUNNER_SHA256", "runner"), ("SUPPORT_SHA256", "gateway_support"),
                           ("SUDOERS_SHA256", "sudoers_draft"), ("CANARY_SHA256", "canary")):
            expected = COMPONENTS[key][1]
            match = re.search(rf"^{label}=([0-9a-f]{{64}})$", installer, re.M)
            pins_ok &= bool(match and match.group(1) == expected)
        checks["installer_pins"] = item("PASS" if pins_ok else "FAIL", "installer embedded pins compared with repository artifacts")
    except OSError as exc:
        checks["installer_pins"] = item("NOT VERIFIED", str(exc))

    try:
        info = subprocess.run(["ctest", "--test-dir", str(BUILD), "--show-only=json-v1", "-L", "privileged"],
                              text=True, capture_output=True, timeout=30)
        data = json.loads(info.stdout) if info.returncode == 0 else {}
        tests = data.get("tests", [])
        names = {test.get("name") for test in tests}
        execs_present = all(pathlib.Path(test["command"][0]).is_file() for test in tests if test.get("command"))
        orchestrator = COMPONENTS["orchestrator"][0].read_text()
        expected_block = re.search(r"^EXPECTED\s*=\s*\((.*?)^\)", orchestrator, re.M | re.S)
        expected_names = set(re.findall(r'^\s+"([^"]+)"[,]?$', expected_block.group(1), re.M)) if expected_block else set()
        exact = len(tests) == 18 and names == expected_names and execs_present
        checks["ctest_inventory"] = item("PASS" if exact else "FAIL",
                                         f"read-only CTest inventory: {len(tests)} privileged entries; executable paths present={execs_present}; exact orchestrator match={names == expected_names}")
    except (OSError, subprocess.SubprocessError, ValueError, KeyError) as exc:
        checks["ctest_inventory"] = item("NOT VERIFIED", str(exc))

    for path in (pathlib.Path("/opt/athena-qualification/build"),
                 pathlib.Path("/usr/local/sbin/athena-alpha-m1-runner"),
                 pathlib.Path("/etc/sudoers.d/athena-alpha-m1")):
        try:
            st = path.lstat()
            expected_uid = (0, 65534) if path == BUILD else (0,)
            expected_mode = {pathlib.Path("/opt/athena-qualification/build"): {0o550, 0o750},
                             pathlib.Path("/usr/local/sbin/athena-alpha-m1-runner"): {0o755},
                             pathlib.Path("/etc/sudoers.d/athena-alpha-m1"): {0o440}}[path]
            valid = not path.is_symlink() and st.st_uid in expected_uid and (st.st_mode & 0o777) in expected_mode
            checks[f"metadata:{path}"] = item("PASS" if valid else "BLOCKED",
                                               f"mode={st.st_mode & 0o777:04o} uid:gid={st.st_uid}:{st.st_gid} type={'symlink' if path.is_symlink() else 'regular' if path.is_file() else 'directory' if path.is_dir() else 'other'}; expected uid in {expected_uid} and mode in {[oct(x) for x in expected_mode]}")
            if path.is_file():
                try:
                    checks[f"installed_hash:{path}"] = item("PASS", sha256(path))
                except OSError as exc:
                    checks[f"installed_hash:{path}"] = item("NOT VERIFIED", str(exc))
        except OSError as exc:
            checks[f"metadata:{path}"] = item("NOT VERIFIED", str(exc))

    visudo = "/usr/sbin/visudo"
    if os.path.exists(visudo):
        try:
            result = subprocess.run([visudo, "-c"], text=True, capture_output=True, timeout=10)
            checks["visudo"] = item("PASS" if result.returncode == 0 else "BLOCKED",
                                    (result.stdout + result.stderr).strip() or f"exit={result.returncode}")
        except (OSError, subprocess.SubprocessError) as exc:
            checks["visudo"] = item("NOT VERIFIED", str(exc))
    else:
        checks["visudo"] = item("NOT VERIFIED", f"missing {visudo}")

    for path in FIXTURES:
        try:
            st = path.lstat()
            with os.scandir(path) as entries:
                first = next(entries, None)
            checks[f"fixture:{path}"] = item("PASS" if first is None else "BLOCKED",
                                             f"mode={st.st_mode & 0o777:04o} uid:gid={st.st_uid}:{st.st_gid}; {'empty' if first is None else 'contains entries; preserved'}")
        except OSError as exc:
            checks[f"fixture:{path}"] = item("NOT VERIFIED", str(exc))

    checks["privileged_campaign"] = item("NOT RUN", "preflight never invokes sudo, runner, or test executables")
    blocked = any(v["status"] in {"BLOCKED", "FAIL", "NOT VERIFIED"} for v in checks.values())
    print(json.dumps({"decision": "BLOCKED" if blocked else "READY FOR PROMOTION APPROVAL", "checks": checks}, indent=2))
    return 1 if blocked else 0


if __name__ == "__main__":
    raise SystemExit(main())
