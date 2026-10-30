#!/usr/bin/env python3
"""Run the non-privileged CTest inventory with bounded time and durable logs.

The driver never selects tests labelled ``privileged`` and never invokes sudo.
CTest keeps responsibility for SKIP_RETURN_CODE and all other test semantics.
"""
import argparse
import datetime as dt
import json
import os
import pathlib
import re
import signal
import subprocess
import sys
import time


RESULT_LINE = re.compile(
    r"^\s*(\d+)/(\d+) Test\s+#(\d+):.*?"
    r"(\*\*\*Skipped|\*\*\*Timeout|\*\*\*Failed|Passed|Failed)\s+"
    r"([0-9.]+) sec\s*$"
)
START_LINE = re.compile(r"^\s*Start\s+#?(\d+):")


def properties(test):
    return {item["name"]: item.get("value") for item in test.get("properties", [])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=pathlib.Path, default=pathlib.Path("build-monad-release"))
    parser.add_argument("--output-dir", type=pathlib.Path, default=pathlib.Path("qualification/alpha-m1-diagnostic"))
    parser.add_argument("--config", help="CTest configuration (auto-detected by default)")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--global-timeout", type=float, default=900)
    parser.add_argument("--default-test-timeout", type=float, default=90)
    parser.add_argument("--parse-console", type=pathlib.Path,
                        help="parse a completed CTest console log instead of executing tests")
    args = parser.parse_args()
    if args.jobs < 1 or args.global_timeout <= 0 or args.default_test_timeout <= 0:
        parser.error("jobs and timeout values must be positive")

    build = args.build_dir.resolve()
    output = args.output_dir.resolve()
    cache = (build / "CMakeCache.txt").read_text()
    cache_values = {}
    for line in cache.splitlines():
        match = re.match(r"([^:#]+):[^=]+=([^=]*)$", line)
        if match:
            cache_values[match.group(1)] = match.group(2)
    config = args.config or cache_values.get("CMAKE_BUILD_TYPE", "")
    if not config:
        configurations = cache_values.get("CMAKE_CONFIGURATION_TYPES", "")
        config = configurations.split(";")[0] if configurations else "Release"
    output.mkdir(parents=True, exist_ok=True)
    timestamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    run_dir = output / timestamp
    run_dir.mkdir()
    ctest = ["ctest", "--test-dir", str(build)]
    inventory = subprocess.run(
        ctest + ["--show-only=json-v1"], capture_output=True, text=True, check=True
    )
    all_tests = json.loads(inventory.stdout)["tests"]
    nonprivileged = [
        test for test in all_tests
        if "privileged" not in properties(test).get("LABELS", [])
    ]
    privileged = [test["name"] for test in all_tests if test not in nonprivileged]
    test_numbers = {test["name"]: index for index, test in enumerate(all_tests, 1)}
    selected = run_dir / "selected-tests.txt"
    selected.write_text("".join(f"{test['name']}\n" for test in nonprivileged))

    command = ctest + (["-C", config] if config else []) + [
        "-LE", "privileged", "--timeout",
        str(args.default_test_timeout), "--output-on-failure", "-j", str(args.jobs),
        "--output-log", str(run_dir / "ctest.log"),
        "--output-junit", str(run_dir / "ctest.xml"),
    ]
    metadata = {
        "started_utc": timestamp,
        "repository": str(build.parent),
        "build_dir": str(build),
        "configuration": config or "single-config-default",
        "total_registered": len(all_tests),
        "selected_nonprivileged": len(nonprivileged),
        "excluded_privileged": privileged,
        "jobs": args.jobs,
        "default_test_timeout_seconds": args.default_test_timeout,
        "global_timeout_seconds": args.global_timeout,
        "command": command,
    }
    (run_dir / "run.json").write_text(json.dumps(metadata, indent=2) + "\n")

    started = time.monotonic()
    timed_out_globally = False
    if args.parse_console:
        console = args.parse_console.read_text()
        summary = re.search(r"Total Test time \(real\) =\s*([0-9.]+) sec", console)
        elapsed = float(summary.group(1)) if summary else 0.0
        process_returncode = 0 if "100% tests passed, 0 tests failed" in console else 8
    else:
        process = subprocess.Popen(
            command, cwd=build, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, start_new_session=True,
        )
        try:
            console, _ = process.communicate(timeout=args.global_timeout)
        except subprocess.TimeoutExpired:
            timed_out_globally = True
            os.killpg(process.pid, signal.SIGTERM)
            try:
                console, _ = process.communicate(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                console, _ = process.communicate()
        elapsed = time.monotonic() - started
        process_returncode = None if timed_out_globally else process.returncode
    (run_dir / "console.log").write_text(console)

    outcomes = {}
    started_tests = set()
    by_number = {index: test for index, test in enumerate(all_tests, 1)}
    selected_numbers = {test_numbers[test["name"]] for test in nonprivileged}
    for line in console.splitlines():
        start_match = START_LINE.match(line)
        if start_match:
            started_tests.add(int(start_match.group(1)))
        match = RESULT_LINE.match(line)
        if not match:
            continue
        _, _, number, raw, duration = match.groups()
        number = int(number)
        if number not in selected_numbers:
            continue
        test = by_number[number]
        name = test["name"]
        status = {
            "Passed": "PASS", "Failed": "FAIL", "***Failed": "FAIL",
            "***Skipped": "SKIP", "***Timeout": "TIMEOUT",
        }[raw]
        props = properties(test)
        skip_code = props.get("SKIP_RETURN_CODE")
        outcomes[name] = {
            "name": name, "ctest_number": number, "result": status,
            "duration_seconds": float(duration),
            "test_exit_code": 0 if status == "PASS" else (int(skip_code) if status == "SKIP" and skip_code is not None else None),
            "timeout_seconds": float(props.get("TIMEOUT", args.default_test_timeout)),
        }

    for test in nonprivileged:
        name = test["name"]
        if name in outcomes:
            continue
        outcomes[name] = {
            "name": name, "ctest_number": test_numbers[name],
            "result": "TIMEOUT" if timed_out_globally and test_numbers[name] in started_tests else "NOT_RUN",
            "duration_seconds": None, "test_exit_code": None,
            "timeout_seconds": float(properties(test).get("TIMEOUT", args.default_test_timeout)),
        }
    for test in all_tests:
        if "privileged" not in properties(test).get("LABELS", []):
            continue
        name = test["name"]
        outcomes[name] = {
            "name": name, "ctest_number": test_numbers[name],
            "result": "NOT_RUN", "duration_seconds": None,
            "test_exit_code": None,
            "timeout_seconds": float(properties(test).get("TIMEOUT", args.default_test_timeout)),
        }
    result_doc = {
        "global_result": "TIMEOUT" if timed_out_globally else ("PASS" if process_returncode == 0 else "FAIL"),
        "global_exit_code": process_returncode,
        "elapsed_seconds": round(elapsed, 3),
        "results": list(outcomes.values()),
    }
    (run_dir / "results.json").write_text(json.dumps(result_doc, indent=2) + "\n")
    with (run_dir / "results.tsv").open("w") as stream:
        stream.write("test\tresult\tduration_seconds\ttest_exit_code\ttimeout_seconds\n")
        for name, result in outcomes.items():
            stream.write(f"{name}\t{result['result']}\t{result['duration_seconds']}\t"
                         f"{result['test_exit_code']}\t{result['timeout_seconds']}\n")
    counts = {key: sum(item["result"] == key for item in outcomes.values())
              for key in ("PASS", "FAIL", "SKIP", "TIMEOUT", "NOT_RUN")}
    print(f"Run directory: {run_dir}")
    print(f"Elapsed: {elapsed:.2f}s; CTest exit: {result_doc['global_exit_code']}; counts: {counts}")
    return 124 if timed_out_globally else process_returncode


if __name__ == "__main__":
    sys.exit(main())
