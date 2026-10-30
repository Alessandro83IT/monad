#!/usr/bin/env python3
"""Safely compare the privileged CTest inventory without evaluating CMake.

This reader accepts only a narrow, literal subset of generated CTest files.
It never invokes CMake or CTest and never evaluates a CMake command. Files that
use `subdirs(...)` or any other unsupported CMake command remain BLOCKED.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import stat
import sys
from pathlib import Path

MANIFEST_SHA256 = "00fd4036bf0f3873f5c2a0e932c912b4709460fa52c35a1c2564016a92811262"
MANIFEST_BASENAME = "alpha-m1-build-reconciliation-session34b-candidate.json"
MAX_FILE_SIZE = 16 * 1024 * 1024
TEST_NAME_RE = re.compile(r"[A-Za-z0-9_.+-]+\Z")


class InventoryError(Exception):
    """Evidence cannot be safely or completely verified."""

    def __init__(self, message: str, evidence: dict[str, object] | None = None) -> None:
        super().__init__(message)
        self.evidence = evidence or {}


class InventoryMismatch(InventoryError):
    """A safely parsed generated inventory differs from the fixed manifest."""

    def __init__(self, message: str, evidence: dict[str, object] | None = None) -> None:
        super().__init__(message)
        self.evidence = evidence or {}


def _read_regular_nofollow(path: str | Path, *, expected_basename: str | None = None) -> bytes:
    """Read a regular file via no-follow directory descriptors, rejecting `..`."""
    raw = os.fspath(path)
    if not os.path.isabs(raw):
        raise InventoryError(f"path must be absolute: {raw}")
    parts = raw.split("/")[1:]
    if not parts or any(part in ("", ".", "..") for part in parts):
        raise InventoryError(f"path contains an empty, dot, or traversal component: {raw}")
    if expected_basename is not None and parts[-1] != expected_basename:
        raise InventoryError(f"manifest basename must be {expected_basename}")

    dir_fd = os.open("/", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    file_fd = None
    try:
        for component in parts[:-1]:
            next_fd = os.open(
                component,
                os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                dir_fd=dir_fd,
            )
            os.close(dir_fd)
            dir_fd = next_fd
        file_fd = os.open(
            parts[-1], os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK,
            dir_fd=dir_fd,
        )
        before = os.fstat(file_fd)
        if not stat.S_ISREG(before.st_mode):
            raise InventoryError(f"not a regular file: {raw}")
        if before.st_size > MAX_FILE_SIZE:
            raise InventoryError(f"file exceeds {MAX_FILE_SIZE} bytes: {raw}")
        data = bytearray()
        while len(data) <= MAX_FILE_SIZE:
            block = os.read(file_fd, min(1024 * 1024, MAX_FILE_SIZE + 1 - len(data)))
            if not block:
                break
            data.extend(block)
        after = os.fstat(file_fd)
        if len(data) > MAX_FILE_SIZE:
            raise InventoryError(f"file exceeds {MAX_FILE_SIZE} bytes: {raw}")
        if (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (
            after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns
        ):
            raise InventoryError(f"file changed while being read: {raw}")
        return bytes(data)
    except OSError as exc:
        raise InventoryError(f"cannot safely read {raw}: {exc}") from exc
    finally:
        if file_fd is not None:
            os.close(file_fd)
        os.close(dir_fd)


RELEVANT_PROPERTIES = {
    "LABELS", "WORKING_DIRECTORY", "TIMEOUT", "SKIP_RETURN_CODE", "RUN_SERIAL",
}
# CMake emits this internal source-location property in generated test files. It
# is inert for test execution and is retained only as validated generator metadata.
INERT_GENERATOR_PROPERTIES = {"_BACKTRACE_TRIPLES"}


def _fixed_manifest(manifest_path: str | Path) -> list[dict[str, object]]:
    raw = _read_regular_nofollow(manifest_path, expected_basename=MANIFEST_BASENAME)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != MANIFEST_SHA256:
        raise InventoryError(f"manifest SHA-256 mismatch: expected {MANIFEST_SHA256}, got {digest}")
    try:
        manifest = json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise InventoryError(f"fixed manifest is not valid UTF-8 JSON: {exc}") from exc
    if not isinstance(manifest, dict) or manifest.get("schema") != 1:
        raise InventoryError("fixed manifest has an unexpected top-level schema")
    builds = manifest.get("builds")
    if not isinstance(builds, list) or len(builds) != 2:
        raise InventoryError("fixed manifest must contain exactly two build records")

    expected_names: list[str] | None = None
    seen_paths: set[str] = set()
    for index, build in enumerate(builds):
        if not isinstance(build, dict):
            raise InventoryError(f"manifest build record {index} is not an object")
        build_path = build.get("path")
        if not isinstance(build_path, str) or not os.path.isabs(build_path) or build_path in seen_paths:
            raise InventoryError(f"manifest build record {index} has an invalid or duplicate build path")
        seen_paths.add(build_path)
        names = build.get("privileged_expected_names_from_orchestrator")
        inv = build.get("ctest_privileged_inventory")
        full = build.get("ctest_full_inventory")
        if not isinstance(names, list) or not isinstance(inv, dict) or not isinstance(full, dict):
            raise InventoryError(f"manifest build record {index} lacks privileged inventory fields")
        if len(names) != 18 or any(not isinstance(n, str) or not TEST_NAME_RE.fullmatch(n) for n in names):
            raise InventoryError(f"manifest build record {index} must contain 18 valid expected names")
        if len(set(names)) != len(names):
            raise InventoryError(f"manifest build record {index} contains duplicate expected names")
        tests = inv.get("tests")
        if not isinstance(tests, list):
            raise InventoryError(f"manifest build record {index} has no structured CTest inventory")
        manifest_names: list[str] = []
        for test in tests:
            if not isinstance(test, dict) or not isinstance(test.get("name"), str):
                raise InventoryError(f"manifest build record {index} has malformed test inventory")
            properties = test.get("properties")
            if not isinstance(properties, list):
                raise InventoryError(f"manifest build record {index} has malformed test properties")
            labels = [p.get("value") for p in properties if isinstance(p, dict) and p.get("name") == "LABELS"]
            if labels == [["privileged"]]:
                manifest_names.append(test["name"])
            elif any("privileged" in label if isinstance(label, list) else label == "privileged" for label in labels):
                raise InventoryError(f"manifest build record {index} has unexpected privileged labels")
        if set(manifest_names) != set(names) or len(manifest_names) != 18:
            raise InventoryError(f"manifest build record {index} expected names disagree with its inventory")
        full_tests = full.get("tests")
        full_names = full.get("names_in_ctest_order")
        count = full.get("count")
        if isinstance(full_tests, list):
            listed_names = [test.get("name") for test in full_tests if isinstance(test, dict)]
            if len(listed_names) == len(full_tests) and len(set(listed_names)) != len(listed_names):
                raise InventoryError(f"manifest build record {index} contains duplicate full test names")
        if (not isinstance(full_tests, list) or not isinstance(full_names, list)
                or not isinstance(count, int) or count != len(full_tests) or count != len(full_names)):
            raise InventoryError(f"manifest build record {index} has an inconsistent full inventory")
        test_names: list[str] = []
        for test in full_tests:
            if not isinstance(test, dict) or not isinstance(test.get("name"), str):
                raise InventoryError(f"manifest build record {index} has malformed full test data")
            name = test["name"]
            if not TEST_NAME_RE.fullmatch(name):
                raise InventoryError(f"manifest build record {index} has invalid test name {name!r}")
            command = test.get("command")
            if not isinstance(command, list) or not command or any(not isinstance(arg, str) for arg in command):
                raise InventoryError(f"manifest build record {index} has malformed command for {name}")
            properties = test.get("properties")
            if not isinstance(properties, list):
                raise InventoryError(f"manifest build record {index} has malformed properties for {name}")
            property_names: set[str] = set()
            for prop in properties:
                if not isinstance(prop, dict) or not isinstance(prop.get("name"), str) or "value" not in prop:
                    raise InventoryError(f"manifest build record {index} has malformed property for {name}")
                prop_name = prop["name"].upper()
                if prop_name not in RELEVANT_PROPERTIES | INERT_GENERATOR_PROPERTIES:
                    raise InventoryError(
                        f"manifest build record {index} has unsupported CTest property {prop_name} for {name}"
                    )
                if prop_name in RELEVANT_PROPERTIES:
                    if prop_name in property_names:
                        raise InventoryError(f"manifest build record {index} repeats {prop_name} for {name}")
                    property_names.add(prop_name)
            test_names.append(name)
        if len(set(test_names)) != len(test_names):
            raise InventoryError(f"manifest build record {index} contains duplicate full test names")
        if full_names != test_names:
            raise InventoryError(f"manifest build record {index} full inventory order disagrees with tests")
        if expected_names is None:
            expected_names = list(names)
        elif names != expected_names:
            raise InventoryError("the two manifest build records disagree on expected name order")
    return builds


def _cmake_commands(text: str) -> list[tuple[str, list[str]]]:
    """Tokenize a restricted CMake command stream without expanding anything."""
    commands: list[tuple[str, list[str]]] = []
    i, n = 0, len(text)

    def skip_space(pos: int) -> int:
        while pos < n and text[pos].isspace():
            pos += 1
        return pos

    def require_boundary(pos: int) -> None:
        if pos < n and not text[pos].isspace() and text[pos] not in ")#" and not text.startswith("\\\n", pos):
            raise InventoryError(f"adjacent CMake argument tokens are not accepted at byte {pos}")

    while True:
        i = skip_space(i)
        if i >= n:
            return commands
        if text[i] == "#":
            end = text.find("\n", i)
            i = n if end < 0 else end + 1
            continue
        match = re.match(r"[A-Za-z_][A-Za-z0-9_]*", text[i:])
        if not match:
            raise InventoryError(f"unsupported CMake syntax at byte {i}")
        command = match.group(0).lower()
        i += len(match.group(0))
        i = skip_space(i)
        if i >= n or text[i] != "(":
            raise InventoryError(f"malformed CMake command {command}")
        i += 1
        args: list[str] = []
        while True:
            i = skip_space(i)
            if i >= n:
                raise InventoryError(f"unterminated CMake command {command}")
            if text[i] == ")":
                i += 1
                break
            if text[i] == "#":
                end = text.find("\n", i)
                i = n if end < 0 else end + 1
                continue
            if text.startswith("\\\n", i):
                i += 2
                continue
            if text[i] == '"':
                i += 1
                value: list[str] = []
                while i < n and text[i] != '"':
                    if text[i] == "\\":
                        i += 1
                        if i >= n:
                            raise InventoryError("trailing escape in quoted CMake argument")
                        if text[i] not in ('"', "\\", ";", " ", "\t", "\n"):
                            raise InventoryError("unsupported escape in quoted CMake argument")
                    if text[i] == "$":
                        raise InventoryError("variable expansion is not allowed in generated inventory")
                    value.append(text[i])
                    i += 1
                if i >= n:
                    raise InventoryError("unterminated quoted CMake argument")
                i += 1
                args.append("".join(value))
                require_boundary(i)
                continue
            if text[i] == "[":
                bracket = re.match(r"\[(=*)\[", text[i:])
                if not bracket:
                    raise InventoryError("unsupported bracket argument syntax")
                close = "]" + bracket.group(1) + "]"
                start = i + len(bracket.group(0))
                end = text.find(close, start)
                if end < 0:
                    raise InventoryError("unterminated bracket argument")
                value = text[start:end]
                if "$" in value:
                    raise InventoryError("variable expansion is not allowed in generated inventory")
                args.append(value)
                i = end + len(close)
                require_boundary(i)
                continue
            start = i
            while i < n and not text[i].isspace() and text[i] not in "()#":
                if text[i] == "$" or text[i] in "\"'":
                    raise InventoryError("unsupported unquoted CMake argument")
                i += 1
            if i == start:
                raise InventoryError(f"unsupported CMake token at byte {i}")
            args.append(text[start:i])
            require_boundary(i)
        if command not in {"add_test", "set_tests_properties"}:
            raise InventoryError(f"unsupported generated CMake command: {command}")
        require_boundary(i)
        commands.append((command, args))


def _parse_inventory(data: bytes) -> tuple[list[str], dict[str, dict[str, object]]]:
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise InventoryError(f"CTestTestfile.cmake is not UTF-8: {exc}") from exc
    commands = _cmake_commands(text)
    tests: dict[str, dict[str, object]] = {}
    order: list[str] = []
    for command, args in commands:
        if command == "add_test":
            if len(args) < 2:
                raise InventoryError("add_test command has too few arguments")
            if args[0].upper() == "NAME":
                if len(args) < 4 or args[2].upper() != "COMMAND":
                    raise InventoryError("unsupported modern add_test form")
                name = args[1]
                command_argv = args[3:]
            else:
                name = args[0]
                command_argv = args[1:]
            if not TEST_NAME_RE.fullmatch(name):
                raise InventoryError(f"invalid literal CTest name: {name!r}")
            if name in tests:
                raise InventoryMismatch(
                    f"duplicate test name: {name}",
                    {"generated_full_test_count": len(order) + 1,
                     "generated_full_names_in_order": [*order, name]},
                )
            if not command_argv:
                raise InventoryError(f"add_test for {name} has no command")
            tests[name] = {"command": command_argv, "properties": {}}
            order.append(name)
        else:
            if "PROPERTIES" not in [arg.upper() for arg in args]:
                raise InventoryError("set_tests_properties lacks PROPERTIES")
            marker = next(i for i, arg in enumerate(args) if arg.upper() == "PROPERTIES")
            names, properties = args[:marker], args[marker + 1:]
            if not names or not properties or len(properties) % 2:
                raise InventoryError("malformed set_tests_properties arguments")
            for name in names:
                if not TEST_NAME_RE.fullmatch(name):
                    raise InventoryError(f"invalid literal property test name: {name!r}")
                test_properties = tests.get(name, {}).get("properties")
                if not isinstance(test_properties, dict):
                    raise InventoryError(f"properties reference missing or future test: {name}")
                for j in range(0, len(properties), 2):
                    key, value = properties[j].upper(), properties[j + 1]
                    if key not in RELEVANT_PROPERTIES | INERT_GENERATOR_PROPERTIES:
                        raise InventoryError(f"unsupported generated CTest property {key} for {name}")
                    if key in INERT_GENERATOR_PROPERTIES:
                        if not re.fullmatch(r"[^;]+;[0-9]+;[^;]+;[^;]+;[0-9]+;[^;]+;[^;]+;[0-9]+;", value):
                            raise InventoryError(f"malformed inert CMake backtrace metadata for {name}")
                        continue
                    if key in RELEVANT_PROPERTIES:
                        if key in test_properties:
                            raise InventoryError(f"duplicate {key} assignment for {name}")
                        if key == "LABELS":
                            value_parts = value.split(";")
                            if any(not label for label in value_parts) or len(set(value_parts)) != len(value_parts):
                                raise InventoryError(f"empty or duplicate label for {name}")
                            test_properties[key] = value_parts
                        elif key == "TIMEOUT":
                            try:
                                test_properties[key] = float(value)
                            except ValueError as exc:
                                raise InventoryError(f"invalid TIMEOUT for {name}") from exc
                        elif key == "SKIP_RETURN_CODE":
                            try:
                                test_properties[key] = int(value)
                            except ValueError as exc:
                                raise InventoryError(f"invalid SKIP_RETURN_CODE for {name}") from exc
                        elif key == "RUN_SERIAL":
                            normalized = value.upper()
                            if normalized not in {"TRUE", "FALSE", "ON", "OFF", "1", "0"}:
                                raise InventoryError(f"invalid RUN_SERIAL value for {name}")
                            test_properties[key] = normalized in {"TRUE", "ON", "1"}
                        else:
                            test_properties[key] = value
    return order, tests


def verify(build_dir: str | Path, manifest_path: str | Path) -> dict[str, object]:
    builds = _fixed_manifest(manifest_path)
    build = os.fspath(build_dir)
    if not os.path.isabs(build):
        raise InventoryError("build directory must be absolute")
    selected = [record for record in builds if record["path"] == build]
    if len(selected) != 1:
        raise InventoryError("build directory does not exactly match one fixed manifest build path",
                             {"requested_build_path": build})
    record = selected[0]
    full = record["ctest_full_inventory"]
    expected_tests = full["tests"]
    expected_names = record["privileged_expected_names_from_orchestrator"]
    # Read the generated file through a no-follow descriptor walk.
    ctest_path = os.path.join(build, "CTestTestfile.cmake")
    generated = _read_regular_nofollow(ctest_path)
    expected_order = full["names_in_ctest_order"]
    expected_map = {test["name"]: test for test in expected_tests}
    base_evidence: dict[str, object] = {
        "manifest_sha256": MANIFEST_SHA256,
        "build_path": build,
        "ctest_file_sha256": hashlib.sha256(generated).hexdigest(),
        "manifest_full_test_count": full["count"],
        "manifest_full_names_in_order": expected_order,
        "manifest_expected_privileged_names": expected_names,
    }
    try:
        generated_order, inventory = _parse_inventory(generated)
    except InventoryMismatch as exc:
        raise InventoryMismatch(str(exc), {**base_evidence, **exc.evidence}) from exc
    except InventoryError as exc:
        raise InventoryError(str(exc), base_evidence) from exc
    generated_evidence = {
        **base_evidence,
        "generated_full_test_count": len(generated_order),
        "generated_full_names_in_order": generated_order,
    }
    if generated_order != expected_order:
        raise InventoryMismatch("generated full inventory names/order differ from fixed manifest",
                                generated_evidence)
    if len(inventory) != full["count"] or len(inventory) != len(expected_tests):
        raise InventoryMismatch("generated full inventory count differs from fixed manifest",
                                generated_evidence)

    command_mismatch: list[str] = []
    property_mismatch: list[str] = []
    for name in expected_order:
        generated_test = inventory[name]
        expected_test = expected_map[name]
        if generated_test["command"] != expected_test["command"]:
            command_mismatch.append(name)
        manifest_props = {
            prop["name"].upper(): prop["value"]
            for prop in expected_test["properties"]
            if prop["name"].upper() in RELEVANT_PROPERTIES
        }
        normalized_props: dict[str, object] = {}
        for key, value in manifest_props.items():
            if key == "LABELS":
                normalized_props[key] = value if isinstance(value, list) else [value]
            elif key == "TIMEOUT":
                normalized_props[key] = float(value)
            elif key == "SKIP_RETURN_CODE":
                normalized_props[key] = int(value)
            elif key == "RUN_SERIAL":
                if not isinstance(value, bool):
                    raise InventoryError(f"manifest RUN_SERIAL for {name} is not boolean")
                normalized_props[key] = value
            else:
                normalized_props[key] = value
        if generated_test["properties"] != normalized_props:
            property_mismatch.append(name)

    generated_privileged = [
        name for name in generated_order
        if "privileged" in inventory[name]["properties"].get("LABELS", [])
    ]
    missing = [name for name in expected_names if name not in generated_privileged]
    unexpected = [name for name in generated_privileged if name not in expected_names]
    wrong_class = [name for name in expected_names if name in inventory and
                   inventory[name]["properties"].get("LABELS") != ["privileged"]]
    if command_mismatch or property_mismatch or missing or unexpected or wrong_class:
        raise InventoryMismatch(
            "generated inventory content differs from fixed manifest: "
            f"command_mismatch={command_mismatch}; property_mismatch={property_mismatch}; "
            f"missing_privileged={missing}; unexpected_privileged={unexpected}; class_mismatch={wrong_class}",
            {
                **generated_evidence,
                "generated_privileged_count": len(generated_privileged),
                "generated_privileged_names_in_order": generated_privileged,
                "command_mismatch": command_mismatch,
                "property_mismatch": property_mismatch,
            },
        )
    return {
        "manifest_sha256": MANIFEST_SHA256,
        "build_path": build,
        "ctest_file_sha256": hashlib.sha256(generated).hexdigest(),
        "manifest_full_test_count": full["count"],
        "generated_full_test_count": len(generated_order),
        "manifest_full_names_in_order": expected_order,
        "generated_full_names_in_order": generated_order,
        "manifest_expected_privileged_names": expected_names,
        "generated_privileged_count": len(generated_privileged),
        "generated_privileged_names_in_order": generated_privileged,
        "status": "PASS",
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--manifest", required=True)
    args = parser.parse_args(argv)
    try:
        result = verify(args.build_dir, args.manifest)
    except InventoryMismatch as exc:
        print(json.dumps({"status": "FAIL", "detail": str(exc), **exc.evidence}, sort_keys=True))
        return 1
    except InventoryError as exc:
        print(json.dumps({"status": "BLOCKED", "detail": str(exc), **exc.evidence}, sort_keys=True))
        return 2
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
