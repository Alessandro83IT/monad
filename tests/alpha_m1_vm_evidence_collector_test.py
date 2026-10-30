#!/usr/bin/env python3
"""Non-privileged safety and fail-closed tests for the VM evidence collector."""

import io
import pathlib
import re
import hashlib
import os
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "qualification" / "alpha-m1-vm-evidence-collector.sh"
SOURCE = SCRIPT.read_text(encoding="utf-8")


class VmEvidenceCollectorTest(unittest.TestCase):
    def invoke(self, *args):
        # Test invocations below deliberately exit during argument validation;
        # the collector is never run against this host as if it were the VM.
        return subprocess.run(
            ["bash", str(SCRIPT), *map(str, args)],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )

    @staticmethod
    def function_source(name):
        match = re.search(rf"(?ms)^{re.escape(name)}\(\) \{{\n(.*?)^\}}", SOURCE)
        if match is None:
            raise AssertionError(f"production function not found: {name}")
        return f"{name}() {{\n{match.group(1)}\n}}"

    @staticmethod
    def helper_python_source():
        match = re.search(
            r"(?ms)^run_pinned_ctest_inventory_helper\(\) \{\n"
            r"\s*timeout .*? <<'PY'\n(.*?)^PY\n\}", SOURCE,
        )
        if match is None:
            raise AssertionError("production pinned-helper Python source not found")
        return match.group(1)

    def test_script_has_valid_bash_syntax(self):
        result = subprocess.run(["bash", "-n", str(SCRIPT)], capture_output=True, text=True)
        self.assertEqual(0, result.returncode, result.stderr)

    def test_runner_candidate_pin_matches_collector_source_bytes(self):
        runner_source = (ROOT / "qualification" / "alpha-m1-verified-evidence-runner.py").read_text(encoding="utf-8")
        match = re.search(r'^EXPECTED_SHA256 = "([0-9a-f]{64})"$', runner_source, re.MULTILINE)
        self.assertIsNotNone(match)
        self.assertEqual(match.group(1), hashlib.sha256(SCRIPT.read_bytes()).hexdigest())

    def test_record_helper_is_defined_before_initial_status_rows(self):
        definition = SOURCE.index("record() {")
        first_call = SOURCE.index("record privileged-ctest-execution")
        self.assertLess(definition, first_call)

    def test_read_only_allowlist_excludes_mutating_commands_and_sudo(self):
        self.assertNotRegex(SOURCE, r"(?m)^\s*(?:sudo|chmod|chown|rm|install|mount|umount|mkfs)(?:\s|$)")
        self.assertNotRegex(
            SOURCE,
            r"(?m)^\s*systemctl\s+(?:reset-failed|start|stop|restart|kill|daemon-reload)(?:\s|$)",
        )
        self.assertNotIn("sudo ", SOURCE)
        self.assertIn("systemctl show", SOURCE)
        self.assertIn("journalctl --no-pager", SOURCE)
        self.assertIn("findmnt -T", SOURCE)
        self.assertIn("getfacl -cp", SOURCE)

    def test_no_ctest_is_invoked_and_manifest_inventory_is_checked(self):
        self.assertNotRegex(SOURCE, r"(?m)^\s*(?:capture|timeout|command)\b[^\n]*\bctest\b")
        self.assertIn("alpha-m1-ctest-inventory.py", SOURCE)
        self.assertIn("run_pinned_ctest_inventory_helper", SOURCE)
        self.assertIn("sys.argv = [helper_path, '--build-dir', build_path, '--manifest', manifest_path]", SOURCE)
        self.assertIn('record "build-$build_tag-ctest-inventory" PASS', SOURCE)
        self.assertIn('record "build-$build_tag-ctest-inventory" BLOCKED', SOURCE)
        self.assertIn("privileged-ctest-execution 'NOT RUN'", SOURCE)

    def test_systemd_inventory_uses_machine_readable_json_without_globs_or_localized_tables(self):
        self.assertIn("--output=json", SOURCE)
        self.assertIn("list-units --all --type=service --output=json", SOURCE)
        self.assertNotIn("systemctl --failed", SOURCE)
        self.assertNotRegex(SOURCE, r"systemctl[^\n]*athena-alpha-m1-\*\.service")
        self.assertIn("parse_systemd_unit_json", SOURCE)
        self.assertIn("capture vm-systemd-version PASS systemctl --version", SOURCE)

    def test_all_inline_python_bootstraps_are_isolated_from_current_directory(self):
        invocations = [
            line for line in SOURCE.splitlines()
            if "python3" in line and "<<'PY'" in line
        ]
        self.assertGreaterEqual(len(invocations), 7)
        for invocation in invocations:
            with self.subTest(invocation=invocation):
                self.assertIn("python3 -I -", invocation)

    def run_systemd_parser(self, units, unit_files):
        match = re.search(r"(?ms)^parse_systemd_unit_json\(\) \{\n(.*?)^\}", SOURCE)
        self.assertIsNotNone(match)
        function = "parse_systemd_unit_json() {\n" + match.group(1) + "\n}"
        with tempfile.TemporaryDirectory(prefix="alpha-m1-systemd-parser-") as temp:
            root = pathlib.Path(temp)
            units_file, files_file, output_file = root / "units.json", root / "files.json", root / "parsed.tsv"
            units_file.write_text(units, encoding="utf-8")
            files_file.write_text(unit_files, encoding="utf-8")
            script = function + '\nparse_systemd_unit_json "$1" "$2" "$3"\n'
            result = subprocess.run(["bash", "-c", script, "systemd-parser-test", str(units_file), str(files_file), str(output_file)], capture_output=True, text=True)
            output = output_file.read_text(encoding="utf-8") if output_file.exists() else ""
            return result, output

    def test_production_systemd_parser_classifies_failed_active_inactive_and_not_loaded(self):
        units = '[{"unit":"athena-alpha-m1-a.service","load":"loaded","active":"failed","sub":"failed"},' \
                '{"unit":"athena-alpha-m1-b.service","load":"loaded","active":"active","sub":"running"},' \
                '{"unit":"athena-alpha-m1-c.service","load":"loaded","active":"inactive","sub":"dead"},' \
                '{"unit":"athena-alpha-m1-e.service","load":"loaded","active":"maintenance","sub":"maintenance"}]'
        files = '[{"unit_file":"athena-alpha-m1-a.service","state":"enabled"},' \
                '{"unit_file":"athena-alpha-m1-b.service","state":"disabled"},' \
                '{"unit_file":"athena-alpha-m1-c.service","state":"static"},' \
                '{"unit_file":"athena-alpha-m1-e.service","state":"enabled"},' \
                '{"unit_file":"athena-alpha-m1-d.service","state":"enabled"}]'
        result, output = self.run_systemd_parser(units, files)
        self.assertEqual(0, result.returncode, result.stderr)
        rows = {row.split("\t")[0]: row.split("\t")[1] for row in output.splitlines()[1:]}
        self.assertEqual({"athena-alpha-m1-a.service": "failed", "athena-alpha-m1-b.service": "active", "athena-alpha-m1-c.service": "inactive", "athena-alpha-m1-d.service": "not-loaded", "athena-alpha-m1-e.service": "maintenance"}, rows)

    def test_production_systemd_parser_accepts_empty_inventory_but_caller_can_block_zero_units(self):
        result, output = self.run_systemd_parser("[]", "[]")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual("unit\tclassification\tload_state\tactive_state\tsub_state\tunit_file_state\n", output)

    def test_production_systemd_parser_blocks_dbus_errors_malformed_incomplete_duplicate_and_unexpected_names(self):
        invalid_cases = [
            ("Failed to connect to bus: Access denied", "[]"),
            ("not-json", "[]"),
            ('[{"unit":"athena-alpha-m1-a.service","load":"loaded","active":"active"}]', "[]"),
            ('[{"load":"loaded","active":"active","sub":"running"}]', "[]"),
            ('[{"unit":"ordinary.service","load":"loaded","active":"active","sub":"running"},{"unit":"athena-alpha-m1-a.service","load":"loaded","active":"active"}]', "[]"),
            ('[{"unit":"athena-alpha-m1-a.service","load":"loaded","active":"active","sub":"running"},{"unit":"athena-alpha-m1-a.service","load":"loaded","active":"active","sub":"running"}]', "[]"),
            ('[{"unit":"athena-alpha-m1-../escape.service","load":"loaded","active":"active","sub":"running"}]', "[]"),
        ]
        for units, files in invalid_cases:
            with self.subTest(units=units):
                result, _ = self.run_systemd_parser(units, files)
                self.assertEqual(2, result.returncode, result.stderr)

    def test_production_process_cgroup_audit_fails_on_stale_session35_unit_process_after_replacement(self):
        historical_match = re.search(r"(?ms)^readonly SESSION35_ALPHA_UNITS='(.*?)'", SOURCE)
        self.assertIsNotNone(historical_match)
        historical = historical_match.group(1).splitlines()
        self.assertEqual(15, len(historical))
        match = re.search(r"(?ms)^audit_alpha_process_cgroups\(\) \{\n(.*?)^\}", SOURCE)
        self.assertIsNotNone(match)
        function = "audit_alpha_process_cgroups() {\n" + match.group(1) + "\n}"
        current = ["athena-alpha-m1-replacement.service", *historical[1:]]
        process = "pid=4321 ppid=1 uid=0 state=S cgroup=0::/system.slice/%s cmd=leftover\n" % historical[0]
        with tempfile.TemporaryDirectory(prefix="alpha-m1-stale-cgroup-") as temp:
            root = pathlib.Path(temp)
            process_file, units_file, output_file = root / "process.txt", root / "units.txt", root / "audit.txt"
            process_file.write_text(process, encoding="utf-8")
            units_file.write_text("\n".join(current) + "\n", encoding="utf-8")
            script = function + '\naudit_alpha_process_cgroups "$1" "$2" "$3" "$4"\n'
            result = subprocess.run(
                ["bash", "-c", script, "stale-cgroup-test", str(process_file), str(units_file), "\n".join(historical), str(output_file)],
                capture_output=True, text=True, check=False,
            )
            output = output_file.read_text(encoding="utf-8")
        self.assertEqual(1, result.returncode, result.stderr)
        self.assertIn("current_id_set_matches_session35=no", output)
        self.assertIn("unaccounted_alpha_cgroup_process=pid=4321", output)
        self.assertIn("unit=" + historical[0], output)
        self.assertIn("status=FAIL", output)

    def test_production_pinned_helper_blocks_replacement_without_executing_it(self):
        match = re.search(r"(?ms)^run_pinned_ctest_inventory_helper\(\) \{\n(.*?)^\}", SOURCE)
        self.assertIsNotNone(match)
        function = "run_pinned_ctest_inventory_helper() {\n" + match.group(1) + "\n}"
        with tempfile.TemporaryDirectory(prefix="alpha-m1-helper-pin-") as temp:
            root = pathlib.Path(temp)
            helper = root / "alpha-m1-ctest-inventory.py"
            marker = root / "replacement-ran"
            digest_file = root / "helper-sha256.txt"
            expected_bytes = b"raise SystemExit(0)\n"
            altered_bytes = b"import pathlib\npathlib.Path(" + repr(str(marker)).encode() + b").touch()\nprint('PASS')\n"
            helper.write_bytes(altered_bytes)
            expected = __import__("hashlib").sha256(expected_bytes).hexdigest()
            script = function + '\nrun_pinned_ctest_inventory_helper "$1" "$2" "$3" "$4" "$5"\n'
            result = subprocess.run(
                ["bash", "-c", script, "helper-pin-test", str(helper), expected, "/tmp/build", "/tmp/manifest.json", str(digest_file)],
                capture_output=True, text=True, check=False,
            )
            digest_evidence = digest_file.read_text(encoding="ascii")
        self.assertEqual(2, result.returncode)
        self.assertFalse(marker.exists())
        self.assertIn("reviewed helper SHA-256 mismatch", result.stderr)
        self.assertIn("expected=" + expected, digest_evidence)
        self.assertIn("observed=" + __import__("hashlib").sha256(altered_bytes).hexdigest(), digest_evidence)

    def test_candidate_directory_guard_and_helper_resolution_are_explicit_and_space_safe(self):
        with tempfile.TemporaryDirectory(prefix="alpha-m1-candidate-path-") as temp:
            root = pathlib.Path(temp)
            candidate = root / "checkout with spaces" / "qualification"
            candidate.mkdir(parents=True)
            link = root / "candidate-link"
            link.symlink_to(candidate, target_is_directory=True)
            function_source = self.function_source("validate_candidate_directory")
            helper_source = self.function_source("candidate_ctest_helper_path")

            result = subprocess.run(
                ["bash", "-c", function_source + "\n" + helper_source +
                 '\nvalidate_candidate_directory "$1" && candidate_ctest_helper_path\n',
                 "candidate-path-test", str(candidate)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertEqual(str(candidate / "alpha-m1-ctest-inventory.py"), result.stdout.strip())

            for invalid in (root / "missing", link, pathlib.Path("relative/candidate")):
                failed = subprocess.run(
                    ["bash", "-c", function_source + '\nvalidate_candidate_directory "$1"\n',
                     "candidate-path-test", str(invalid)],
                    capture_output=True, text=True, check=False,
                )
                self.assertEqual(64, failed.returncode, (invalid, failed.stderr))

        self.assertNotIn("SCRIPT_DIR", SOURCE)
        self.assertIn('candidate_ctest_helper_path)', SOURCE)
        self.assertIn('run_pinned_ctest_inventory_helper "$(candidate_ctest_helper_path)"', SOURCE)

    def test_candidate_checks_do_not_expand_the_diagnostic_read_set(self):
        self.assertIn("readonly D6_PARENT='/var/tmp/athena-d67-ext4'", SOURCE)
        self.assertIn("readonly D6_RESIDUAL=\"$D6_PARENT/athena-d6-8yRDjc\"", SOURCE)
        self.assertIn("'d6-parent|/var/tmp/athena-d67-ext4'", SOURCE)
        self.assertIn("'d6-residual|/var/tmp/athena-d67-ext4/athena-d6-8yRDjc'", SOURCE)
        self.assertIn('capture_path "$safe_label" "$path" "$safe_label"', SOURCE)
        self.assertNotRegex(SOURCE, r"(?m)^\s*(?:capture|capture_group)\s+[^\n]*D6")
        self.assertNotIn('sha256sum -- "$D6_PARENT', SOURCE)
        self.assertNotIn('cat -- "$D6_RESIDUAL', SOURCE)
        self.assertIn('for archive_root in "$BACKUPS" "$LOG_ROOT"; do', SOURCE)
        self.assertIn('-maxdepth 4 -type f', SOURCE)

    def test_pinned_helper_executes_only_hashed_candidate_bytes_and_handles_spaces(self):
        with tempfile.TemporaryDirectory(prefix="alpha-m1-pinned-helper-") as temp:
            root = pathlib.Path(temp)
            candidate = root / "candidate dir"
            candidate.mkdir()
            helper = candidate / "alpha-m1-ctest-inventory.py"
            marker = root / "helper-ran.txt"
            source = f"from pathlib import Path\nPath({str(marker)!r}).write_text('verified')\n".encode()
            helper.write_bytes(source)
            helper.chmod(0o600)
            digest_file = root / "helper.sha256"
            digest = hashlib.sha256(source).hexdigest()
            bash_source = self.function_source("candidate_ctest_helper_path") + "\n" + self.function_source("run_pinned_ctest_inventory_helper")
            result = subprocess.run(
                ["bash", "-c", bash_source + '''
CANDIDATE_DIR=$1
run_pinned_ctest_inventory_helper "$(candidate_ctest_helper_path)" "$2" "$3" "$4" "$5"
''', "pinned-helper-test", str(candidate), digest, str(root), str(root / "manifest.json"), str(digest_file)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertEqual("verified", marker.read_text())
            self.assertIn("observed=" + digest, digest_file.read_text())

    def test_pinned_helper_blocks_missing_symlink_hardlink_and_digest_write_errors(self):
        with tempfile.TemporaryDirectory(prefix="alpha-m1-helper-failures-") as temp:
            root = pathlib.Path(temp)
            helper = root / "helper.py"
            helper.write_text("raise SystemExit(0)\n")
            digest = hashlib.sha256(helper.read_bytes()).hexdigest()
            digest_file = root / "helper.sha256"
            script = self.function_source("run_pinned_ctest_inventory_helper") + '''
run_pinned_ctest_inventory_helper "$1" "$2" "$3" "$4" "$5"
'''

            missing = subprocess.run(
                ["bash", "-c", script, "helper-failure", str(root / "missing"), digest,
                 str(root), str(root / "manifest"), str(digest_file)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(2, missing.returncode)

            link = root / "helper-link.py"
            link.symlink_to(helper)
            linked = subprocess.run(
                ["bash", "-c", script, "helper-failure", str(link), digest,
                 str(root), str(root / "manifest"), str(digest_file)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(2, linked.returncode)

            hardlink = root / "helper-hardlink.py"
            os.link(helper, hardlink)
            hardlinked = subprocess.run(
                ["bash", "-c", script, "helper-failure", str(helper), digest,
                 str(root), str(root / "manifest"), str(digest_file)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(2, hardlinked.returncode)
            self.assertIn("exactly one hard link", hardlinked.stderr)

            write_failed = subprocess.run(
                ["bash", "-c", script, "helper-failure", str(helper), digest,
                 str(root), str(root / "manifest"), str(root / "absent" / "digest")],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(2, write_failed.returncode)

    def test_pinned_helper_detects_in_place_replacement_during_read(self):
        helper_code = self.helper_python_source()
        with tempfile.TemporaryDirectory(prefix="alpha-m1-helper-race-") as temp:
            root = pathlib.Path(temp)
            helper = root / "helper.py"
            marker = root / "unverified-executed"
            original = b"raise SystemExit(0)\n"
            replacement = f"from pathlib import Path\nPath({str(marker)!r}).touch()\n".encode()
            helper.write_bytes(original)
            expected = hashlib.sha256(original).hexdigest()
            digest_output = root / "digest.txt"
            real_read = os.read
            changed = False

            def replace_after_open(fd, count):
                nonlocal changed
                data = real_read(fd, count)
                if not changed and data:
                    changed = True
                    helper.write_bytes(replacement)
                return data

            argv_backup = sys.argv
            sys.argv = ["-", str(helper), expected, str(root), str(root / "manifest"), str(digest_output)]
            namespace = {"__name__": "__main__", "__file__": str(helper)}
            try:
                with mock.patch("os.read", side_effect=replace_after_open), \
                     mock.patch("sys.stderr", new_callable=io.StringIO) as stderr:
                    with self.assertRaises(SystemExit) as raised:
                        exec(compile(helper_code, "<pinned-helper-test>", "exec"), namespace, namespace)
            finally:
                sys.argv = argv_backup
            self.assertEqual(2, raised.exception.code)
            self.assertTrue(changed)
            self.assertFalse(marker.exists())
            self.assertIn("changed while being read", stderr.getvalue())

    def test_root_execution_is_refused_before_output_creation(self):
        self.assertLess(SOURCE.index('if refuse_root_execution "$EUID"'), SOURCE.index("mkdir -m 700 -- \"$OUT\""))
        self.assertIn("refuses root execution", SOURCE)
        match = re.search(r"(?ms)^refuse_root_execution\(\) \{\n(.*?)^\}", SOURCE)
        self.assertIsNotNone(match)
        function = "refuse_root_execution() {\n" + match.group(1) + "\n}"
        result = subprocess.run(
            ["bash", "-c", function + '\nrefuse_root_execution 0\n', "root-guard-test"],
            capture_output=True, text=True, check=False,
        )
        self.assertEqual(77, result.returncode)
        self.assertIn("refuses root execution", result.stderr)

    def test_malformed_and_missing_arguments_fail_before_output_creation(self):
        with tempfile.TemporaryDirectory(prefix="alpha-m1-args-test-") as temp:
            parent = pathlib.Path(temp) / "output-parent"
            parent.mkdir()
            parent.chmod(0o700)
            cases = [
                ((), "--output-parent"),
                (("--output-parent", str(parent)), "--candidate-dir"),
                (("--output-parent", str(parent), "--candidate-dir"), "--candidate-dir"),
                (("--output-parent", str(parent), "--unknown-option"), "unknown argument"),
            ]
            for args, message in cases:
                result = self.invoke(*args)
                self.assertEqual(64, result.returncode, result.stderr)
                self.assertIn(message, result.stderr)
                self.assertEqual([], list(parent.iterdir()))

    def test_build_inspection_is_symlink_gated_and_artifact_failures_propagate(self):
        self.assertIn("check_path_components \"$required_path\"", SOURCE)
        self.assertIn("if [[ $safe_build == true ]]; then", SOURCE)
        self.assertIn("if not stat.S_ISREG(st.st_mode)", SOURCE)
        self.assertIn("one or more observed artifact hashes differ from manifest", SOURCE)
        self.assertIn("source revision observation does not cryptographically prove source-to-binary provenance", SOURCE)

    def test_candidate_paths_resolve_to_exact_six_session_34b_files(self):
        match = re.search(r"(?ms)^candidate_files=\(\n(.*?)^\)", SOURCE)
        self.assertIsNotNone(match)
        candidate_array = "candidate_files=(\n" + match.group(1) + ")\n"
        candidate_dir = ROOT / "qualification"
        snippet = candidate_array + '''
for item in "${candidate_files[@]}"; do
    filename=${item#*|}
    printf '%s\\n' "$CANDIDATE_DIR/$filename"
done
'''
        result = subprocess.run(
            ["bash", "-c", snippet],
            env={"CANDIDATE_DIR": str(candidate_dir)},
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(0, result.returncode, result.stderr)
        expected = [
            "install-athena-alpha-m1-admin.sh",
            "alpha-m1-build-reconciliation-session34b-candidate.json",
            "athena-alpha-m1-runner.py",
            "gateway_support.py",
            "athena-alpha-m1-systemd-canary.py",
            "athena-alpha-m1-sudoers",
        ]
        paths = result.stdout.splitlines()
        self.assertEqual([str(candidate_dir / name) for name in expected], paths)
        self.assertEqual(6, len(paths))
        for path, filename in zip(paths, expected):
            self.assertEqual(path, str(candidate_dir / filename))
            self.assertTrue(pathlib.Path(path).is_file())

    def test_writes_are_scoped_to_new_private_evidence_directory(self):
        self.assertIn("umask 077", SOURCE)
        self.assertIn('mkdir -m 700 -- "$OUT"', SOURCE)
        self.assertIn("nothing was overwritten", SOURCE)
        self.assertIn('>"$OUT/', SOURCE)
        self.assertNotIn(">/var/", SOURCE)
        self.assertNotIn(">/opt/", SOURCE)
        self.assertNotIn(">/sys/", SOURCE)
        self.assertNotIn(">/proc/", SOURCE)

    def test_help_does_not_start_collection_or_create_output(self):
        result = self.invoke("--help")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIn("never invokes sudo", result.stdout)
        self.assertIn("CTest cases", result.stdout)

    def test_container_prefixes_are_fail_closed(self):
        case_block = SOURCE[SOURCE.index("case \"$virt_result\" in"):SOURCE.index("capture vm-machine-id")]
        self.assertIn("container-*", case_block)
        self.assertIn("docker-*", case_block)
        self.assertIn("podman-*", case_block)
        self.assertIn("record vm-environment FAIL", case_block)

    def test_report_header_does_not_execute_backtick_text_from_path_or_path_lookup(self):
        match = re.search(r"(?ms)^write_report_header\(\) \{\n(.*?)^\}", SOURCE)
        self.assertIsNotNone(match)
        function = "write_report_header() {\n" + match.group(1) + "\n}"
        self.assertNotRegex(function, r'"[^"\n]*`[^"\n]*`')
        with tempfile.TemporaryDirectory(prefix="alpha-m1-header-test-") as temp:
            root = pathlib.Path(temp)
            bindir = root / "bin"
            bindir.mkdir()
            marker = root / "command-was-executed"
            for command in ("collector.sha256", "sentinel-output", "sentinel-collector"):
                path = bindir / command
                path.write_text("#!/bin/sh\ntouch \"$MARKER\"\n", encoding="utf-8")
                path.chmod(0o700)
            report = root / "report.md"
            script = function + '\nwrite_report_header "$1" "$2" "$3" "$4"\n'
            result = subprocess.run(
                ["/bin/bash", "-c", script, "header-test", str(report), "sentinel-output", "sentinel-collector", "2026-10-21T00:00:00Z"],
                env={"PATH": str(bindir), "MARKER": str(marker)},
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertFalse(marker.exists())
            self.assertIn("sentinel-output", report.read_text(encoding="utf-8"))
            self.assertIn("collector-sha256.txt", report.read_text(encoding="utf-8"))

    def test_symlink_output_parent_fails_closed_without_touching_target(self):
        with tempfile.TemporaryDirectory(prefix="alpha-m1-collector-test-") as temp:
            root = pathlib.Path(temp)
            real_parent = root / "real-parent"
            real_parent.mkdir()
            link_parent = root / "linked-parent"
            link_parent.symlink_to(real_parent, target_is_directory=True)
            result = self.invoke("--output-parent", link_parent, "--candidate-dir", ROOT / "qualification")
            self.assertEqual(64, result.returncode, result.stderr)
            self.assertIn("No collection was started", result.stderr)
            self.assertEqual([], list(real_parent.iterdir()))

    def test_symlink_candidate_directory_fails_closed_before_output_creation(self):
        with tempfile.TemporaryDirectory(prefix="alpha-m1-collector-test-") as temp:
            root = pathlib.Path(temp)
            parent = root / "output-parent"
            parent.mkdir()
            candidate_link = root / "candidate-link"
            candidate_link.symlink_to(ROOT / "qualification", target_is_directory=True)
            result = self.invoke("--output-parent", parent, "--candidate-dir", candidate_link)
            self.assertEqual(64, result.returncode, result.stderr)
            self.assertIn("No collection was started", result.stderr)
            self.assertEqual([], list(parent.iterdir()))

    def test_group_writable_output_parent_fails_closed_without_writing(self):
        with tempfile.TemporaryDirectory(prefix="alpha-m1-collector-test-") as temp:
            parent = pathlib.Path(temp) / "shared-parent"
            parent.mkdir(mode=0o700)
            parent.chmod(0o770)
            result = self.invoke("--output-parent", parent, "--candidate-dir", ROOT / "qualification")
            self.assertEqual(64, result.returncode, result.stderr)
            self.assertIn("not group/world writable", result.stderr)
            self.assertEqual([], list(parent.iterdir()))

    def test_pinned_manifest_contains_exact_privileged_inventory_for_both_builds(self):
        import json

        manifest_path = ROOT / "qualification" / "alpha-m1-build-reconciliation-session34b-candidate.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        expected = re.search(r"readonly EXPECTED_PRIVILEGED='(.*?)'", SOURCE, re.S).group(1).splitlines()
        self.assertEqual(2, len(manifest["builds"]))
        for build in manifest["builds"]:
            inventory = build["ctest_privileged_inventory"]
            self.assertEqual(18, inventory["count"])
            self.assertEqual(expected, inventory["names_in_ctest_order"])

    def test_existing_candidate_bytes_match_session_34b_fixed_baseline(self):
        expected = {
            "install-athena-alpha-m1-admin.sh": "2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7",
            "alpha-m1-build-reconciliation-session34b-candidate.json": "00fd4036bf0f3873f5c2a0e932c912b4709460fa52c35a1c2564016a92811262",
            "athena-alpha-m1-runner.py": "c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def",
            "gateway_support.py": "92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514",
            "athena-alpha-m1-systemd-canary.py": "9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2",
            "athena-alpha-m1-sudoers": "263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18",
        }
        for filename, digest in expected.items():
            self.assertEqual(digest, __import__("hashlib").sha256((ROOT / "qualification" / filename).read_bytes()).hexdigest())


if __name__ == "__main__":
    unittest.main()
