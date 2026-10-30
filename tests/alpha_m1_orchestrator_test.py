#!/usr/bin/env python3
"""Unit tests for the non-privileged Alpha M1 orchestration driver."""

import importlib.util
import json
import pathlib
import subprocess
import tempfile
import unittest
from unittest import mock


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "qualification" / "run-alpha-m1-privileged.py"
SPEC = importlib.util.spec_from_file_location("alpha_m1_orchestrator", SCRIPT)
DRIVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DRIVER)


class OrchestratorTest(unittest.TestCase):
    def test_runs_all_cases_in_order_and_records_failure_evidence(self):
        inventory = {"tests": [{"name": name, "properties": [{"name": "LABELS", "value": ["privileged"]}]}
                     for name in DRIVER.EXPECTED]}
        outputs = [subprocess.CompletedProcess([], 0, stdout=json.dumps(inventory))]
        outputs.extend(subprocess.CompletedProcess([], 9 if i == 6 else 0,
                                                   stdout=(f"case={DRIVER.EXPECTED[i]} status={9 if i == 6 else 0} "
                                                           f"classification={'TEST_FAIL' if i == 6 else 'PASS'} "
                                                           f"timeout=false termination_confirmed=true error=false "
                                                           f"log=/tmp/{i}.json manifest=x manifest_sha256={'a' * 64}\n"
                                                           f"case-{i}\n").encode())
                       for i in range(18))
        with tempfile.TemporaryDirectory() as temp:
            out_dir = pathlib.Path(temp) / "results"
            with mock.patch("sys.argv", [str(SCRIPT), "--build-dir", temp,
                                          "--output-dir", str(out_dir)]), \
                    mock.patch.object(DRIVER.subprocess, "run", side_effect=outputs) as run:
                self.assertEqual(1, DRIVER.main())
            calls = run.call_args_list
            self.assertEqual(19, len(calls))
            self.assertTrue(all(call.args[0][0] == "sudo" for call in calls[1:]))
            self.assertEqual(list(DRIVER.EXPECTED), [call.args[0][-1] for call in calls[1:]])
            run_dir = next(out_dir.iterdir())
            result = json.loads((run_dir / "results.json").read_text())
            self.assertEqual(18, result["total"])
            self.assertEqual("TEST_FAIL", result["results"][6]["result"])
            self.assertTrue((run_dir / result["results"][6]["log"]).read_bytes().endswith(b"case-6\n"))

    def test_exit_codes_keep_skip_timeout_and_infrastructure_distinct(self):
        inventory = {"tests": [{"name": name, "properties": [{"name": "LABELS", "value": ["privileged"]}]}
                     for name in DRIVER.EXPECTED]}
        codes = [0, 77, 125, 2, 124] + [0] * 13
        outputs = [subprocess.CompletedProcess([], 0, stdout=json.dumps(inventory))]
        runner_classes = ["PASS", "SKIP", "INFRA_ERROR", "INFRA_ERROR", "TIMEOUT"] + ["PASS"] * 13
        outputs.extend(subprocess.CompletedProcess([], code,
                            stdout=(f"case={DRIVER.EXPECTED[i]} status={code} classification={runner_classes[i]} "
                                    f"timeout={'true' if runner_classes[i] == 'TIMEOUT' else 'false'} "
                                    f"termination_confirmed=true error=false "
                                    f"log=/tmp/{i}.json manifest=x manifest_sha256={'b' * 64}\n"
                                    f"code-{code}\n").encode()) for i, code in enumerate(codes))
        with tempfile.TemporaryDirectory() as temp:
            out_dir = pathlib.Path(temp) / "results"
            with mock.patch("sys.argv", [str(SCRIPT), "--build-dir", temp,
                                          "--output-dir", str(out_dir)]), \
                    mock.patch.object(DRIVER.subprocess, "run", side_effect=outputs):
                self.assertEqual(1, DRIVER.main())
            run_dir = next(out_dir.iterdir())
            result = json.loads((run_dir / "results.json").read_text())
            self.assertEqual(["PASS", "SKIP", "INFRA_ERROR", "INFRA_ERROR", "TIMEOUT"],
                             [item["result"] for item in result["results"][:5]])
            self.assertEqual(0, result["test_failed"])
            self.assertEqual(2, result["infra_errors"])
            self.assertEqual(1, result["skipped"])
            self.assertEqual(4, result["failed"])
            self.assertEqual(1, result["timed_out"])

    def test_sudo_exit_one_without_runner_marker_is_infrastructure(self):
        inventory = {"tests": [{"name": name, "properties": [{"name": "LABELS", "value": ["privileged"]}]}
                     for name in DRIVER.EXPECTED]}
        outputs = [subprocess.CompletedProcess([], 0, stdout=json.dumps(inventory))]
        outputs.extend([subprocess.CompletedProcess([], 1, stdout=b"sudo: a password is required\n")]
                       + [subprocess.CompletedProcess([], 77, stdout=(
                           f"case={name} status=77 classification=SKIP timeout=false termination_confirmed=true "
                           f"error=false log=/tmp/skip.json manifest=x manifest_sha256={'c' * 64}\n").encode())
                          for name in DRIVER.EXPECTED[1:]])
        with tempfile.TemporaryDirectory() as temp:
            out_dir = pathlib.Path(temp) / "results"
            with mock.patch("sys.argv", [str(SCRIPT), "--build-dir", temp,
                                          "--output-dir", str(out_dir)]), \
                    mock.patch.object(DRIVER.subprocess, "run", side_effect=outputs):
                self.assertEqual(1, DRIVER.main())
            run_dir = next(out_dir.iterdir())
            result = json.loads((run_dir / "results.json").read_text())
            self.assertEqual("INFRA_ERROR", result["results"][0]["result"])
            self.assertFalse(result["results"][0]["runner_started"])

    def test_inventory_mismatch_refuses_to_invoke_sudo(self):
        inventory = {"tests": []}
        completed = subprocess.CompletedProcess([], 0, stdout=json.dumps(inventory))
        with tempfile.TemporaryDirectory() as temp:
            with mock.patch("sys.argv", [str(SCRIPT), "--build-dir", temp,
                                          "--output-dir", str(pathlib.Path(temp) / "results")]), \
                    mock.patch.object(DRIVER.subprocess, "run", return_value=completed) as run:
                self.assertEqual(2, DRIVER.main())
            self.assertEqual(1, run.call_count)
            run_dir = next((pathlib.Path(temp) / "results").iterdir())
            result = json.loads((run_dir / "results.json").read_text())
            self.assertEqual(18, result["blocked"])
            self.assertEqual(0, result["not_run"])
            self.assertTrue(all(item["result"] == "BLOCKED" for item in result["results"]))

    def test_timeout_requires_runner_proof_and_exit_124_can_be_a_test_failure(self):
        classify = DRIVER.classify_runner_result
        self.assertEqual("TEST_FAIL", classify(
            timed_out=False, code=124, runner_started=True, runner_error=False,
            runner_classification="TEST_FAIL", runner_timed_out=False,
            termination_confirmed=True))
        self.assertEqual("TERMINATION_UNCERTAIN", classify(
            timed_out=True, code=124, runner_started=False, runner_error=False,
            runner_classification=None, runner_timed_out=False,
            termination_confirmed=False))
        self.assertEqual("TIMEOUT", classify(
            timed_out=False, code=124, runner_started=True, runner_error=False,
            runner_classification="TIMEOUT", runner_timed_out=True,
            termination_confirmed=True))


if __name__ == "__main__":
    unittest.main()
