from __future__ import annotations

import contextlib
import hashlib
import importlib.util
import io
import json
import pathlib
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[1]
HELPER = ROOT / "qualification" / "alpha-m1-ctest-inventory.py"
MANIFEST = ROOT / "qualification" / "alpha-m1-build-reconciliation-session34b-candidate.json"
SPEC = importlib.util.spec_from_file_location("alpha_m1_ctest_inventory", HELPER)
inventory = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(inventory)


def bracket(value: str) -> str:
    return "[=[" + value + "]=]"


class CTestInventoryTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temp.name)
        self.build = self.root / "build"
        self.build.mkdir()
        self.manifest = self.root / MANIFEST.name
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        self.record = manifest["builds"][0]
        self.old_build = self.record["path"]
        self.record["path"] = str(self.build)
        for test in self.record["ctest_full_inventory"]["tests"]:
            test["command"] = [arg.replace(self.old_build, str(self.build)) for arg in test["command"]]
            for prop in test["properties"]:
                if prop["name"] == "WORKING_DIRECTORY" and isinstance(prop["value"], str):
                    prop["value"] = prop["value"].replace(self.old_build, str(self.build))
        self.manifest_obj = manifest
        self.write_manifest()
        self.hash_patch = mock.patch.object(
            inventory, "MANIFEST_SHA256", hashlib.sha256(self.manifest.read_bytes()).hexdigest()
        )
        self.hash_patch.start()
        self.write_generated()

    def tearDown(self) -> None:
        self.hash_patch.stop()
        self.temp.cleanup()

    def write_manifest(self) -> None:
        self.manifest.write_text(json.dumps(self.manifest_obj), encoding="utf-8")
        if hasattr(self, "hash_patch"):
            inventory.MANIFEST_SHA256 = hashlib.sha256(self.manifest.read_bytes()).hexdigest()

    def write_generated(self, tests: list[dict] | None = None) -> None:
        tests = self.record["ctest_full_inventory"]["tests"] if tests is None else tests
        rows = ["# fixed literal generated fixture"]
        for test in tests:
            rows.append("add_test(" + " ".join(bracket(arg) for arg in [test["name"], *test["command"]]) + ")")
            relevant: list[str] = []
            for prop in test["properties"]:
                key, value = prop["name"].upper(), prop["value"]
                if key not in inventory.RELEVANT_PROPERTIES:
                    continue
                if isinstance(value, list):
                    value = ";".join(str(item) for item in value)
                relevant.extend((key, str(value)))
            if relevant:
                rows.append("set_tests_properties(" + bracket(test["name"]) + " PROPERTIES " +
                            " ".join(bracket(item) for item in relevant) + ")")
        (self.build / "CTestTestfile.cmake").write_text("\n".join(rows) + "\n", encoding="utf-8")

    def verify(self) -> dict[str, object]:
        return inventory.verify(self.build, self.manifest)

    def test_exact_full_ordered_manifest_inventory_passes(self) -> None:
        result = self.verify()
        expected = self.record["ctest_full_inventory"]
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["build_path"], str(self.build))
        self.assertEqual(result["manifest_full_test_count"], expected["count"])
        self.assertEqual(result["generated_full_names_in_order"], expected["names_in_ctest_order"])
        self.assertEqual(result["ctest_file_sha256"], hashlib.sha256(
            (self.build / "CTestTestfile.cmake").read_bytes()).hexdigest())

    def test_build_path_must_match_fixed_manifest_entry(self) -> None:
        with self.assertRaisesRegex(inventory.InventoryError, "does not exactly match"):
            inventory.verify(str(self.root / "other-build"), self.manifest)

    def test_missing_full_test_is_fail(self) -> None:
        self.write_generated(self.record["ctest_full_inventory"]["tests"][:-1])
        with self.assertRaises(inventory.InventoryMismatch):
            self.verify()

    def test_unexpected_nonprivileged_test_is_fail(self) -> None:
        tests = list(self.record["ctest_full_inventory"]["tests"])
        tests.append({"name": "unexpected_ordinary_test", "command": ["/bin/true"], "properties": []})
        self.write_generated(tests)
        with self.assertRaises(inventory.InventoryMismatch):
            self.verify()

    def test_unexpected_privileged_test_is_fail(self) -> None:
        tests = list(self.record["ctest_full_inventory"]["tests"])
        tests.append({"name": "unexpected_privileged_test", "command": ["/bin/true"],
                      "properties": [{"name": "LABELS", "value": ["privileged"]}]})
        self.write_generated(tests)
        with self.assertRaises(inventory.InventoryMismatch):
            self.verify()

    def test_generated_order_mismatch_is_fail(self) -> None:
        tests = list(self.record["ctest_full_inventory"]["tests"])
        tests[0], tests[1] = tests[1], tests[0]
        self.write_generated(tests)
        with self.assertRaisesRegex(inventory.InventoryMismatch, "names/order"):
            self.verify()

    def test_command_path_and_arguments_are_pinned(self) -> None:
        tests = json.loads(json.dumps(self.record["ctest_full_inventory"]["tests"]))
        privileged = next(test for test in tests if test["name"] in
                          self.record["privileged_expected_names_from_orchestrator"])
        privileged["command"][0] = "/tmp/attacker-substituted-binary"
        privileged["command"].append("unexpected-argument")
        self.write_generated(tests)
        with self.assertRaisesRegex(inventory.InventoryMismatch, "command_mismatch"):
            self.verify()

    def test_relevant_property_mismatch_is_fail(self) -> None:
        tests = json.loads(json.dumps(self.record["ctest_full_inventory"]["tests"]))
        test = tests[0]
        working_directory = next(prop for prop in test["properties"] if prop["name"] == "WORKING_DIRECTORY")
        working_directory["value"] = "/tmp/elsewhere"
        self.write_generated(tests)
        with self.assertRaisesRegex(inventory.InventoryMismatch, "property_mismatch"):
            self.verify()

    def test_run_serial_boolean_is_pinned(self) -> None:
        tests = json.loads(json.dumps(self.record["ctest_full_inventory"]["tests"]))
        test = next(item for item in tests if item["name"] == "public_planning_test")
        run_serial = next(prop for prop in test["properties"] if prop["name"] == "RUN_SERIAL")
        self.assertIs(run_serial["value"], True)
        run_serial["value"] = False
        self.write_generated(tests)
        with self.assertRaisesRegex(inventory.InventoryMismatch, "property_mismatch"):
            self.verify()

    def test_unsupported_generated_or_manifest_property_is_blocked(self) -> None:
        ctest = self.build / "CTestTestfile.cmake"
        ctest.write_text(ctest.read_text(encoding="utf-8") +
                         'set_tests_properties([=[store_metadata_errors_test]=] PROPERTIES '
                         '[=[UNRECOGNIZED_PROPERTY]=] [=[anything]=])\n', encoding="utf-8")
        with self.assertRaisesRegex(inventory.InventoryError, "unsupported generated CTest property"):
            self.verify()

        test = self.record["ctest_full_inventory"]["tests"][0]
        test["properties"].append({"name": "UNKNOWN_MANIFEST_PROPERTY", "value": "x"})
        self.write_manifest()
        with self.assertRaisesRegex(inventory.InventoryError, "unsupported CTest property"):
            self.verify()

    def test_duplicate_generated_test_name_is_fail(self) -> None:
        tests = list(self.record["ctest_full_inventory"]["tests"])
        tests.append(tests[0])
        self.write_generated(tests)
        with self.assertRaisesRegex(inventory.InventoryMismatch, "duplicate test name"):
            self.verify()

    def test_manifest_duplicate_full_test_and_order_disagreement_are_blocked(self) -> None:
        full = self.record["ctest_full_inventory"]
        full["tests"].append(full["tests"][0])
        self.write_manifest()
        with self.assertRaisesRegex(inventory.InventoryError, "duplicate full test"):
            self.verify()

        full["tests"].pop()
        full["names_in_ctest_order"][0], full["names_in_ctest_order"][1] = (
            full["names_in_ctest_order"][1], full["names_in_ctest_order"][0]
        )
        self.write_manifest()
        with self.assertRaisesRegex(inventory.InventoryError, "order disagrees"):
            self.verify()

    def test_label_class_mismatch_is_fail(self) -> None:
        tests = json.loads(json.dumps(self.record["ctest_full_inventory"]["tests"]))
        name = self.record["privileged_expected_names_from_orchestrator"][0]
        test = next(item for item in tests if item["name"] == name)
        labels = next(prop for prop in test["properties"] if prop["name"] == "LABELS")
        labels["value"] = ["ordinary"]
        self.write_generated(tests)
        with self.assertRaises(inventory.InventoryMismatch):
            self.verify()

    def test_unsupported_incomplete_and_adjacent_cmake_are_blocked(self) -> None:
        cases = (
            "execute_process(COMMAND /bin/touch /tmp/must-not-exist)\n",
            'subdirs([=[nested]=])\n',
            "add_test(NAME injected COMMAND /bin/true\n",
            'add_test([=[name]=]"/bin/true")\n',
            "add_test(NAME ${DYNAMIC_NAME} COMMAND /bin/true)\n",
        )
        for content in cases:
            with self.subTest(content=content):
                (self.build / "CTestTestfile.cmake").write_text(content, encoding="utf-8")
                with self.assertRaises(inventory.InventoryError):
                    self.verify()
        self.assertFalse(pathlib.Path("/tmp/must-not-exist").exists())

    def test_manifest_hash_is_fixed(self) -> None:
        inventory.MANIFEST_SHA256 = "0" * 64
        with self.assertRaisesRegex(inventory.InventoryError, "SHA-256 mismatch"):
            self.verify()

    def test_symlink_build_component_and_ctest_file_are_blocked(self) -> None:
        alias = self.root / "build-alias"
        alias.symlink_to(self.build, target_is_directory=True)
        self.record["path"] = str(alias)
        self.write_manifest()
        with self.assertRaisesRegex(inventory.InventoryError, "cannot safely read"):
            inventory.verify(alias, self.manifest)

        self.record["path"] = str(self.build)
        self.write_manifest()
        target = self.build / "inventory-copy"
        target.write_bytes((self.build / "CTestTestfile.cmake").read_bytes())
        ctest = self.build / "CTestTestfile.cmake"
        ctest.unlink()
        ctest.symlink_to(target)
        with self.assertRaisesRegex(inventory.InventoryError, "cannot safely read"):
            self.verify()

    def test_symlink_manifest_component_is_blocked(self) -> None:
        alias = self.root / "manifest-link" / MANIFEST.name
        alias.parent.mkdir()
        alias.symlink_to(self.manifest)
        with self.assertRaisesRegex(inventory.InventoryError, "cannot safely read"):
            inventory.verify(self.build, alias)

    def test_cli_exit_codes_distinguish_pass_fail_blocked(self) -> None:
        out, err = io.StringIO(), io.StringIO()
        args = ["--build-dir", str(self.build), "--manifest", str(self.manifest)]
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            self.assertEqual(inventory.main(args), 0)
        self.assertEqual(json.loads(out.getvalue())["status"], "PASS")

        self.write_generated(self.record["ctest_full_inventory"]["tests"][:-1])
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            self.assertEqual(inventory.main(args), 1)
        mismatch_evidence = json.loads(out.getvalue())
        self.assertEqual(mismatch_evidence["status"], "FAIL")
        self.assertEqual(mismatch_evidence["build_path"], str(self.build))
        self.assertEqual(mismatch_evidence["generated_full_test_count"],
                         self.record["ctest_full_inventory"]["count"] - 1)
        self.assertEqual(len(mismatch_evidence["generated_full_names_in_order"]),
                         mismatch_evidence["generated_full_test_count"])

        (self.build / "CTestTestfile.cmake").write_text("execute_process()\n", encoding="utf-8")
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            self.assertEqual(inventory.main(args), 2)
        blocked_evidence = json.loads(out.getvalue())
        self.assertEqual(blocked_evidence["status"], "BLOCKED")
        self.assertEqual(blocked_evidence["build_path"], str(self.build))
        self.assertIn("ctest_file_sha256", blocked_evidence)


if __name__ == "__main__":
    unittest.main()
