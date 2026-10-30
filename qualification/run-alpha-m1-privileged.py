#!/usr/bin/env python3
"""Run the exact Alpha M1 privileged inventory through its restricted sudo rule."""

import argparse
import datetime as dt
import json
import pathlib
import re
import subprocess
import sys
import time


RUNNER = "/usr/local/sbin/athena-alpha-m1-runner"
EXPECTED = (
    "activation_execution_privileged_sources_test",
    "activation_execution_privileged_legacy_test",
    "activation_execution_privileged_owner_test",
    "activation_execution_privileged_bind_test",
    "activation_execution_privileged_nested_test",
    "activation_execution_privileged_ext4_test",
    "activation_plan_retention_privileged_owner_test",
    "activation_plan_retention_privileged_bind_test",
    "activation_plan_retention_privileged_nested_test",
    "activation_plan_retention_privileged_ext4_test",
    "activation_plan_persistence_privileged_test",
    "legacy_d3_scanner_test",
    "activation_plan_persistence_privileged_owner_test",
    "activation_plan_persistence_privileged_bind_test",
    "activation_plan_persistence_privileged_nested_test",
    "activation_plan_persistence_privileged_tmpfs_test",
    "activation_plan_persistence_privileged_ext4_test",
    "initial_activation_migration_test",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=pathlib.Path,
                        default=pathlib.Path("/opt/athena-qualification/build"))
    parser.add_argument("--output-dir", type=pathlib.Path,
                        default=pathlib.Path("qualification/alpha-m1-privileged-results"))
    parser.add_argument("--case-timeout", type=float, default=240,
                        help="outer timeout per sudo/runner invocation (default: 240 seconds)")
    args = parser.parse_args()
    if args.case_timeout <= 0:
        parser.error("--case-timeout must be positive")

    build = args.build_dir.resolve()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    run_dir = args.output_dir.resolve() / stamp
    run_dir.mkdir()
    (run_dir / "selected-tests.txt").write_text("".join(name + "\n" for name in EXPECTED))
    try:
        inventory = subprocess.run(["ctest", "--test-dir", str(build), "--show-only=json-v1"],
                                   check=True, capture_output=True, text=True)
        registered = json.loads(inventory.stdout)["tests"]
    except Exception as exc:
        reason = f"Inventory unavailable; no cases launched: {type(exc).__name__}: {exc}\n"
        (run_dir / "preflight.log").write_text(reason)
        results = [{"case": case, "command": None, "exit_code": None, "result": "NOT_RUN",
                    "reason": "preflight infrastructure failure", "started_ns": None,
                    "ended_ns": None, "duration_seconds": 0.0, "log": "preflight.log"}
                   for case in EXPECTED]
        _write_report(run_dir, stamp, build, args.case_timeout, results)
        print(reason, file=sys.stderr, end="")
        return 2
    def labels(test):
        for prop in test.get("properties", []):
            if prop.get("name") == "LABELS":
                value = prop.get("value", [])
                return set(value if isinstance(value, list) else [value])
        return set()

    actual = tuple(test["name"] for test in registered if "privileged" in labels(test))
    if actual != EXPECTED:
        print("Refusing to run: privileged CTest inventory differs from the reviewed 18-case list.",
              file=sys.stderr)
        print("Expected: " + ", ".join(EXPECTED), file=sys.stderr)
        print("Found:    " + ", ".join(actual), file=sys.stderr)
        reason = "Privileged inventory differs from the reviewed allowlist; no cases launched.\n"
        (run_dir / "preflight.log").write_text(reason)
        results = [{"case": case, "command": None, "exit_code": None, "result": "BLOCKED",
                    "reason": "privileged inventory mismatch", "started_ns": None,
                    "ended_ns": None, "duration_seconds": 0.0, "log": "preflight.log"}
                   for case in EXPECTED]
        _write_report(run_dir, stamp, build, args.case_timeout, results)
        return 2
    results = []
    for index, case in enumerate(EXPECTED, 1):
        started = time.time_ns()
        begin = time.monotonic()
        timed_out = False
        command = ["sudo", "-n", RUNNER, case]
        try:
            process = subprocess.run(command, stdin=subprocess.DEVNULL,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     timeout=args.case_timeout, check=False)
            code = process.returncode
            output = process.stdout
        except subprocess.TimeoutExpired as exc:
            timed_out = True
            code = 124
            output = (exc.stdout or b"") + b"\nOrchestrator timeout expired.\n"
        # A runner completion marker proves sudo reached the allowlisted runner.
        # Without it, even exit 1 is a sudo/launch infrastructure failure.
        text_output = output.decode("utf-8", errors="replace") if isinstance(output, bytes) else str(output)
        marker = re.search(
            r"(?m)^case=" + re.escape(case) +
            r" status=(-?\d+) classification=([A-Z_]+) timeout=(true|false)"
            r" termination_confirmed=(true|false) error=(true|false)"
            r" log=\S+ manifest=\S+ manifest_sha256=\S+\s*$",
            text_output)
        runner_started = marker is not None
        runner_error = marker is not None and marker.group(5) == "true"
        runner_classification = marker.group(2) if marker else None
        termination_confirmed = marker is not None and marker.group(4) == "true"
        classification = classify_runner_result(
            timed_out=timed_out, code=code, runner_started=runner_started,
            runner_error=runner_error, runner_classification=runner_classification,
            runner_timed_out=bool(marker is not None and marker.group(3) == "true"),
            termination_confirmed=termination_confirmed)
        ended = time.time_ns()
        elapsed = time.monotonic() - begin
        log_name = f"{index:02d}-{case}.log"
        (run_dir / log_name).write_bytes(output)
        result = {"case": case, "command": command, "exit_code": code,
                  "result": classification,
                  "runner_started": runner_started,
                  "runner_classification": runner_classification,
                  "termination_confirmed": termination_confirmed,
                  "started_ns": started, "ended_ns": ended,
                  "duration_seconds": round(elapsed, 6), "log": log_name}
        results.append(result)
        print(f"[{index:02d}/18] {case}: {result['result']} exit={code} "
              f"duration={elapsed:.3f}s log={log_name}")

    _write_report(run_dir, stamp, build, args.case_timeout, results)
    return 0 if all(r["result"] == "PASS" for r in results) else 1


def _write_report(run_dir, stamp, build, case_timeout, results):
    summary = {"schema": 1, "started_utc": stamp, "build_dir": str(build),
               "runner": RUNNER, "case_timeout_seconds": case_timeout,
               "total": len(results), "passed": sum(r["result"] == "PASS" for r in results),
               # Keep legacy `failed` as a count of all non-pass outcomes.
               "failed": sum(r["result"] != "PASS" for r in results),
               "test_failed": sum(r["result"] == "TEST_FAIL" for r in results),
               "infra_errors": sum(r["result"] == "INFRA_ERROR" for r in results),
               "skipped": sum(r["result"] == "SKIP" for r in results),
               "blocked": sum(r["result"] == "BLOCKED" for r in results),
               "not_run": sum(r["result"] == "NOT_RUN" for r in results),
               "timed_out": sum(r["result"] == "TIMEOUT" for r in results),
               "termination_uncertain": sum(r["result"] == "TERMINATION_UNCERTAIN" for r in results),
               "results": results}
    (run_dir / "results.json").write_text(json.dumps(summary, indent=2) + "\n")
    with (run_dir / "results.tsv").open("w", encoding="utf-8") as stream:
        stream.write("case\tresult\texit_code\tduration_seconds\tlog\n")
        for result in results:
            stream.write(f"{result['case']}\t{result['result']}\t{result['exit_code']}\t"
                         f"{result['duration_seconds']}\t{result['log']}\n")
    print(f"Results: {run_dir / 'results.json'}")


def classify_runner_result(*, timed_out, code, runner_started, runner_error,
                           runner_classification, runner_timed_out,
                           termination_confirmed):
    """Classify only from runner proof; client timeout is not unit termination."""
    if timed_out:
        return "TIMEOUT" if runner_started and termination_confirmed else "TERMINATION_UNCERTAIN"
    if not runner_started:
        return "INFRA_ERROR"
    if (not runner_started or runner_error or not termination_confirmed
            or runner_classification in ("INFRA_ERROR", "TERMINATION_UNCERTAIN", "STOP_REQUESTED",
                                         "EVIDENCE_ERROR")):
        return "INFRA_ERROR" if termination_confirmed else "TERMINATION_UNCERTAIN"
    if runner_classification == "TIMEOUT" and runner_timed_out and code == 124:
        return "TIMEOUT"
    if code == 0 and runner_classification == "PASS":
        return "PASS"
    if code == 77 and runner_classification == "SKIP":
        return "SKIP"
    if runner_classification in ("PASS", "SKIP", "TEST_FAIL"):
        return "TEST_FAIL"
    return "INFRA_ERROR"
    return summary


if __name__ == "__main__":
    raise SystemExit(main())
