#!/usr/bin/env python3
"""Create a read-only reconciliation manifest for Alpha M1 builds.

Only CTest inventory queries are run; no test case, canary, sudo command, or
administrative operation is invoked. The manifest is evidence of observations,
not build authentication or promotion approval.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import pathlib
import re
import stat
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_PROTECTED = pathlib.Path("/opt/athena-qualification/build")
DEFAULT_ISOLATED = ROOT / "build-monad-debug"
INSTALLER = ROOT / "qualification/install-athena-alpha-m1-admin.sh"
ORCHESTRATOR = ROOT / "qualification/run-alpha-m1-privileged.py"
BUILD_ARTIFACTS = (
    "activation_execution_privileged_test",
    "activation_plan_retention_test",
    "activation_plan_persistence_test",
    "legacy_d3_test",
    "initial_activation_test",
    "libathena_core.so",
    "CTestTestfile.cmake",
)
SOURCE_DIRS = {"src", "include", "tests", "cmake", "lib", "app", "qualification", "scripts"}
SOURCE_SUFFIXES = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx",
    ".cmake", ".py", ".sh", ".txt", ".json", ".toml", ".in",
}
EXCLUDED_PARTS = {
    ".git", "__pycache__", "alpha-m1-privileged-results",
    "alpha-m1-diagnostic-20261020", "alpha-m1-diagnostic",
}
PIN_NAMES = {
    "runner": ("qualification/athena-alpha-m1-runner.py", "RUNNER_SHA256"),
    "gateway_support": ("qualification/gateway_support.py", "SUPPORT_SHA256"),
    "canary": ("qualification/athena-alpha-m1-systemd-canary.py", "CANARY_SHA256"),
    "sudoers_draft": ("qualification/athena-alpha-m1-sudoers", "SUDOERS_SHA256"),
}


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_source_input(relative: pathlib.Path) -> str | None:
    """Hash a checkout input without following any path-component symlink."""
    directory_fd = os.open(ROOT, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW)
    file_fd = None
    try:
        for component in relative.parts[:-1]:
            next_fd = os.open(component, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                              dir_fd=directory_fd)
            os.close(directory_fd)
            directory_fd = next_fd
        try:
            file_fd = os.open(relative.parts[-1], os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW,
                              dir_fd=directory_fd)
        except FileNotFoundError:
            return None
        metadata = os.fstat(file_fd)
        if not stat.S_ISREG(metadata.st_mode):
            raise RuntimeError(f"source input is not a regular non-symlink file: {relative}")
        digest = hashlib.sha256()
        while True:
            block = os.read(file_fd, 1024 * 1024)
            if not block:
                break
            digest.update(block)
        return digest.hexdigest()
    except OSError as exc:
        raise RuntimeError(f"cannot safely hash source input {relative}: {exc}") from exc
    finally:
        if file_fd is not None:
            os.close(file_fd)
        os.close(directory_fd)


def run_readonly(argv: list[str], timeout: int = 60) -> dict:
    try:
        result = subprocess.run(argv, cwd=ROOT, text=True, capture_output=True,
                                timeout=timeout, check=False)
        return {"argv": argv, "exit_code": result.returncode,
                "stdout": result.stdout, "stderr": result.stderr}
    except (OSError, subprocess.SubprocessError) as exc:
        return {"argv": argv, "exit_code": None, "stdout": "", "stderr": str(exc)}


def git_output(*args: str) -> str:
    result = subprocess.run(["git", *args], cwd=ROOT, text=True,
                            capture_output=True, check=True)
    return result.stdout.strip()


def source_input_path(relative: pathlib.Path) -> bool:
    if any(part in EXCLUDED_PARTS or part.startswith("build-") or part == "build-alpha"
           for part in relative.parts):
        return False
    if (relative.parent == pathlib.Path("qualification")
            and relative.name.startswith("alpha-m1-build-reconciliation-")
            and relative.suffix == ".json"):
        return False
    if relative.name in {"CMakeLists.txt", "CMakePresets.json", "CMakeUserPresets.json"}:
        return True
    if not relative.parts:
        return False
    return relative.parts[0] in SOURCE_DIRS and relative.suffix in SOURCE_SUFFIXES


def source_provenance() -> dict:
    head = git_output("rev-parse", "HEAD")
    branch = git_output("branch", "--show-current")
    tracked_raw = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT,
                                 capture_output=True, check=True).stdout
    tracked = [pathlib.Path(os.fsdecode(item)) for item in tracked_raw.split(b"\0") if item]
    untracked_raw = subprocess.run(
        ["git", "ls-files", "--others", "--exclude-standard", "-z"], cwd=ROOT,
        capture_output=True, check=True).stdout
    untracked = [pathlib.Path(os.fsdecode(item)) for item in untracked_raw.split(b"\0") if item]
    status = git_output("status", "--short", "--untracked-files=all")

    tracked_inputs = []
    for rel in tracked:
        if source_input_path(rel):
            tracked_inputs.append({"path": rel.as_posix(), "sha256": sha256_source_input(rel)})
    untracked_inputs = []
    omitted_untracked = []
    for rel in untracked:
        if source_input_path(rel):
            untracked_inputs.append({"path": rel.as_posix(), "sha256": sha256_source_input(rel)})
        else:
            omitted_untracked.append(rel.as_posix())
    tree_payload = json.dumps(
        {"tracked_source_inputs": tracked_inputs, "untracked_source_inputs": untracked_inputs},
        sort_keys=True, separators=(",", ":"),
    ).encode()
    return {
        "repository_root": str(ROOT), "branch": branch, "head": head,
        "working_tree_status_short": status,
        "source_input_snapshot_sha256": sha256_bytes(tree_payload),
        "tracked_source_inputs": tracked_inputs,
        "untracked_source_inputs": untracked_inputs,
        "untracked_paths_outside_source_input_filter": omitted_untracked,
        "scope_limitation": (
            "Snapshot hashes tracked files and untracked source/config files under the listed source roots "
            "plus root CMake entry points. It excludes generated build trees, evidence/results, caches, "
            "and untracked non-source files. A CMakeCache source directory and matching hashes do not "
            "prove that a binary was built from this snapshot; build logs or an authenticated build "
            "record are still required."
        ),
    }


def embedded_pins() -> dict:
    text = INSTALLER.read_text()
    pins = {}
    for name, (relative, variable) in PIN_NAMES.items():
        match = re.search(rf"^{variable}=([0-9a-f]{{64}})$", text, re.M)
        pins[name] = {"path": relative, "sha256": match.group(1) if match else None}
    block = re.search(r"done <<'HASHES'\n(.*?)\nHASHES", text, re.S)
    build_pins = {}
    if block:
        for line in block.group(1).splitlines():
            match = re.fullmatch(r"([A-Za-z0-9_.+-]+) ([0-9a-f]{64})", line.strip())
            if match:
                build_pins[match.group(1)] = match.group(2)
    return {"admin_artifact_pins": pins, "protected_build_artifact_pins": build_pins}


def cache_value(cache: dict[str, str], key: str) -> str | None:
    return cache.get(key)


def read_cache(build: pathlib.Path) -> tuple[dict[str, str], str | None]:
    path = build / "CMakeCache.txt"
    if not path.is_file():
        return {}, None
    values = {}
    for line in path.read_text(errors="replace").splitlines():
        if line.startswith("//") or line.startswith("#") or ":" not in line:
            continue
        key_type, _, value = line.partition("=")
        key, _, _type = key_type.partition(":")
        values[key] = value
    return values, sha256_file(path)


def ctest_inventory(build: pathlib.Path, label: str | None) -> dict:
    argv = ["ctest", "--test-dir", str(build), "--show-only=json-v1"]
    if label:
        argv.extend(["-L", label])
    observed = run_readonly(argv)
    tests = []
    parse_error = None
    if observed["exit_code"] == 0:
        try:
            data = json.loads(observed["stdout"])
            tests = data.get("tests", [])
        except (json.JSONDecodeError, AttributeError) as exc:
            parse_error = str(exc)
    names = [test.get("name") for test in tests]
    normalized = json.dumps(tests, sort_keys=True, separators=(",", ":")).encode()
    return {
        "command": observed["argv"], "exit_code": observed["exit_code"],
        "stderr": observed["stderr"], "parse_error": parse_error,
        "count": len(tests), "names_in_ctest_order": names,
        "inventory_sha256": sha256_bytes(normalized), "tests": tests,
        "status": "PASS" if observed["exit_code"] == 0 and parse_error is None else "NOT VERIFIED",
    }


def inspect_build(name: str, build_arg: pathlib.Path, pins: dict, expected_source_root: pathlib.Path | None,
                  approval_reference: str | None = None) -> dict:
    build = build_arg.expanduser().resolve()
    cache, cache_hash = read_cache(build)
    compiler = cache_value(cache, "CMAKE_CXX_COMPILER")
    compiler_version = None
    if compiler:
        probe = run_readonly([compiler, "--version"], timeout=10)
        compiler_version = {
            "command": probe["argv"], "exit_code": probe["exit_code"],
            "first_line": (probe["stdout"] or probe["stderr"]).splitlines()[0]
            if (probe["stdout"] or probe["stderr"]).splitlines() else None,
        }
    build_pins = pins["protected_build_artifact_pins"]
    artifact_records = []
    for artifact in BUILD_ARTIFACTS:
        path = build / artifact
        actual = sha256_file(path) if path.is_file() else None
        expected = build_pins.get(artifact)
        artifact_records.append({
            "name": artifact, "path": str(path), "sha256": actual,
            "runner_installer_pin_sha256": expected,
            "matches_runner_installer_pin": actual == expected if expected else None,
        })
    admin_hashes = {}
    for key, pin in pins["admin_artifact_pins"].items():
        path = ROOT / pin["path"]
        actual = sha256_file(path) if path.is_file() else None
        admin_hashes[key] = {"path": pin["path"], "actual_sha256": actual,
                             "installer_pin_sha256": pin["sha256"],
                             "matches_installer_pin": actual == pin["sha256"] if pin["sha256"] else False}
    full = ctest_inventory(build, None)
    privileged = ctest_inventory(build, "privileged")
    privileged_expected = []
    try:
        orchestrator = ORCHESTRATOR.read_text()
        expected_block = re.search(r"^EXPECTED\s*=\s*\((.*?)^\)", orchestrator, re.M | re.S)
        if expected_block:
            privileged_expected = re.findall(r'^\s+"([^"]+)",?$', expected_block.group(1), re.M)
    except OSError:
        pass
    return {
        "role": name, "path": str(build),
        "operator_approval_reference": approval_reference,
        "promotion_performed": False,
        "build_directory_exists": build.is_dir(),
        "cmake_cache": {
            "sha256": cache_hash,
            "build_type": cache_value(cache, "CMAKE_BUILD_TYPE"),
            "cxx_compiler": compiler,
            "cxx_compiler_version_probe": compiler_version,
            "cxx_compiler_id": cache_value(cache, "CMAKE_CXX_COMPILER_ID"),
            "cxx_compiler_version": cache_value(cache, "CMAKE_CXX_COMPILER_VERSION"),
            "source_directory": cache_value(cache, "CMAKE_HOME_DIRECTORY"),
            "source_directory_matches_checkout": (
                pathlib.Path(cache["CMAKE_HOME_DIRECTORY"]).resolve() == expected_source_root.resolve()
                if cache.get("CMAKE_HOME_DIRECTORY") and expected_source_root else None
            ),
        },
        "artifacts": artifact_records,
        "all_required_build_artifacts_present": all(item["sha256"] for item in artifact_records),
        "all_build_artifacts_match_current_installer_pins": all(
            item["matches_runner_installer_pin"] is True for item in artifact_records
        ),
        "admin_artifact_hashes_vs_installer_pins": admin_hashes,
        "all_admin_hashes_match_installer_pins": all(x["matches_installer_pin"] for x in admin_hashes.values()),
        "ctest_full_inventory": full,
        "ctest_privileged_inventory": privileged,
        "privileged_inventory_matches_orchestrator": privileged["names_in_ctest_order"] == privileged_expected,
        "privileged_expected_names_from_orchestrator": privileged_expected,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--protected-build", type=pathlib.Path, default=DEFAULT_PROTECTED)
    parser.add_argument("--isolated-build", type=pathlib.Path, default=DEFAULT_ISOLATED)
    parser.add_argument("--candidate-build", type=pathlib.Path,
                        help="optional candidate build; requires a human approval reference")
    parser.add_argument("--approval-reference", help="review record/ticket identifying explicit candidate approval")
    parser.add_argument("--output", type=pathlib.Path, required=True,
                        help="new manifest path; existing files are never overwritten")
    args = parser.parse_args()
    if bool(args.candidate_build) != bool(args.approval_reference):
        parser.error("--candidate-build and --approval-reference must be supplied together")

    pins = embedded_pins()
    source = source_provenance()
    builds = [
        inspect_build("protected", args.protected_build, pins, None),
        inspect_build("isolated_updated", args.isolated_build, pins, ROOT),
    ]
    if args.candidate_build:
        builds.append(inspect_build("explicitly_approved_candidate", args.candidate_build,
                                    pins, ROOT, args.approval_reference))
    manifest = {
        "schema": 1,
        "generated_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
        "purpose": "read-only build/source/hash/inventory reconciliation; not promotion or qualification",
        "source_provenance": source,
        "installer_pin_source": str(INSTALLER.relative_to(ROOT)),
        "installer_pin_source_sha256": sha256_file(INSTALLER),
        "orchestrator_sha256": sha256_file(ORCHESTRATOR),
        "pins": pins,
        "builds": builds,
        "promotion": {
            "performed": False,
            "candidate_present": any(b["role"] == "explicitly_approved_candidate" for b in builds),
            "warning": "This manifest does not approve, authenticate, promote, or install any build or pin.",
        },
    }
    output = args.output.expanduser()
    if not output.is_absolute():
        output = ROOT / output
    output = output.resolve()
    if output.exists():
        print(f"Refusing to overwrite existing manifest: {output}", file=sys.stderr)
        return 2
    output.parent.mkdir(parents=True, exist_ok=True)
    data = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    try:
        fd = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
    except OSError as exc:
        print(f"Cannot create manifest without overwrite: {exc}", file=sys.stderr)
        return 2
    print(f"manifest={output}")
    print(f"manifest_sha256={sha256_file(output)}")
    for build in builds:
        print(f"{build['role']}: full={build['ctest_full_inventory']['count']} "
              f"privileged={build['ctest_privileged_inventory']['count']} "
              f"privileged_exact={build['privileged_inventory_matches_orchestrator']} "
              f"build_pins_match={build['all_build_artifacts_match_current_installer_pins']} "
              f"source={build['cmake_cache']['source_directory']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
