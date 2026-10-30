#!/usr/bin/env python3
"""Static consistency checks for Alpha M1's privileged command boundary."""

import ast
import hashlib
import pathlib
import re
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
QUAL = ROOT / "qualification"
PINNED_ARTIFACTS = (
    ("RUNNER_SHA256", "athena-alpha-m1-runner.py"),
    ("SUPPORT_SHA256", "gateway_support.py"),
    ("SUDOERS_SHA256", "athena-alpha-m1-sudoers"),
    ("CANARY_SHA256", "athena-alpha-m1-systemd-canary.py"),
)


def installer_pin_mismatches(installer, qualification_dir):
    mismatches = []
    for variable, filename in PINNED_ARTIFACTS:
        match = re.search(rf"^{variable}=([0-9a-f]{{64}})$", installer, re.MULTILINE)
        if match is None:
            raise AssertionError(f"{variable} is missing or malformed")
        actual = hashlib.sha256((qualification_dir / filename).read_bytes()).hexdigest()
        if actual != match.group(1):
            mismatches.append((variable, actual, match.group(1)))
    return mismatches


def assignment(path, name):
    tree = ast.parse(path.read_text(encoding="utf-8"))
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(
                isinstance(target, ast.Name) and target.id == name for target in node.targets):
            return ast.literal_eval(node.value)
    raise AssertionError(f"assignment {name} not found in {path}")


class AdminStaticTest(unittest.TestCase):
    def test_allowlist_runner_orchestrator_and_sudoers_are_exactly_aligned(self):
        runner = (QUAL / "athena-alpha-m1-runner.py").read_text(encoding="utf-8")
        allowlist = assignment(QUAL / "athena-alpha-m1-runner.py", "ALLOWLIST")
        orchestrated = assignment(QUAL / "run-alpha-m1-privileged.py", "EXPECTED")
        sudoers = (QUAL / "athena-alpha-m1-sudoers").read_text(encoding="utf-8")
        names = tuple(re.findall(r"athena-alpha-m1-runner ([a-z0-9_]+)", sudoers))
        self.assertEqual(18, len(names))
        self.assertEqual(18, len(set(names)))
        self.assertEqual(set(allowlist), set(names))
        self.assertEqual(set(names), set(orchestrated))
        self.assertIn('exe_name, mode = authorize_case(sys.argv[1], ALLOWLIST)', runner)
        self.assertIn('str(error is not None).lower()', runner)
        self.assertNotRegex(sudoers, r"\*|\?|\[")

    def test_fixture_path_is_consistent_across_admin_components(self):
        expected = "/var/lib/athena-alpha-m1/ext4-fixture"
        runner = (QUAL / "athena-alpha-m1-runner.py").read_text(encoding="utf-8")
        canary = (QUAL / "athena-alpha-m1-systemd-canary.py").read_text(encoding="utf-8")
        installer = (QUAL / "install-athena-alpha-m1-admin.sh").read_text(encoding="utf-8")
        self.assertIn(f'Path("{expected}")', runner)
        self.assertIn(f'EXT4_PARENT = "{expected}"', canary)
        self.assertIn(f"FIXTURE={expected}", installer)
        self.assertIn("EXT4_PARENT]", canary)

    def test_runner_omits_openat2_blocking_filter_and_canary_checks_compensating_controls(self):
        runner = (QUAL / "athena-alpha-m1-runner.py").read_text(encoding="utf-8")
        canary = (QUAL / "athena-alpha-m1-systemd-canary.py").read_text(encoding="utf-8")
        self.assertNotIn('"--property=RestrictSUIDSGID=yes"', runner)
        self.assertNotIn('"--property=RestrictSUIDSGID=yes"', canary)
        for source in (runner, canary):
            self.assertIn('"--property=NoNewPrivileges=yes"', source)
            self.assertIn('"--property=PrivateMounts=yes"', source)
            self.assertIn('"--property=CapabilityBoundingSet=CAP_SYS_ADMIN CAP_CHOWN"', source)
        self.assertIn('if "nosuid" not in options:', canary)
        self.assertIn('("/tmp", ext4_parent)', canary)

    def test_runner_binds_control_group_to_exact_unit_identity(self):
        runner = (QUAL / "athena-alpha-m1-runner.py").read_text(encoding="utf-8")
        self.assertIn('"--property=Id"', runner)
        self.assertIn('if unit_id != expected_id:', runner)
        self.assertIn('expected_cgroup = "/system.slice/" + expected_id', runner)
        self.assertIn('if control_group not in ("", expected_cgroup):', runner)

    def test_installer_hash_pins_match_candidates_and_reject_a_bad_fixture_pin(self):
        installer = (QUAL / "install-athena-alpha-m1-admin.sh").read_text(encoding="utf-8")
        runbook = (ROOT / "docs/qualification/alpha-m1-admin-staging.md").read_text(encoding="utf-8")
        self.assertEqual([], installer_pin_mismatches(installer, QUAL))

        # Prove a stale pin is rejected using copied artifacts and installer
        # text in a private temporary fixture; repository files stay untouched.
        with tempfile.TemporaryDirectory(prefix="alpha-m1-pin-fixture-") as temp_dir:
            fixture = pathlib.Path(temp_dir)
            for _, filename in PINNED_ARTIFACTS:
                (fixture / filename).write_bytes((QUAL / filename).read_bytes())
            bad_installer = re.sub(
                r"(?m)^(RUNNER_SHA256=)([0-9a-f])",
                lambda match: match.group(1) + ("0" if match.group(2) != "0" else "1"),
                installer,
                count=1,
            )
            self.assertNotEqual(installer, bad_installer)
            self.assertEqual(["RUNNER_SHA256"], [
                item[0] for item in installer_pin_mismatches(bad_installer, fixture)
            ])

        self.assertIn("non è stato aggiornato automaticamente", runbook)
        self.assertIn("promozione revisionata", runbook)
        self.assertIn("qualsiasi differenza interrompe la procedura", runbook)

    def test_installer_checks_hashes_before_receipt_and_installation(self):
        installer = (QUAL / "install-athena-alpha-m1-admin.sh").read_text(encoding="utf-8")
        checks = installer.index('[[ $(sha256 "$RUNNER_SRC") == "$RUNNER_SHA256" ]]')
        receipt = installer.index("check_canary_receipt", installer.index("check_prereqs()"))
        install = installer.index('install "${local_install[@]}" -m 0755 -- "$RUNNER_SRC"')
        self.assertLess(checks, receipt)
        self.assertLess(receipt, install)
        self.assertIn("shutil.which('systemctl', path='/usr/sbin:/usr/bin:/sbin:/bin')", installer)
        self.assertNotIn("/usr/bin/systemd'", installer)
        self.assertRegex(installer, r"(?m)^SUPPORT_SHA256=[0-9a-f]{64}$")
        self.assertIn('"support:$SUPPORT_DST"', installer)
        runner = (QUAL / "athena-alpha-m1-runner.py").read_text(encoding="utf-8")
        self.assertIn('with_name("gateway_support.py")', runner)

    def test_installer_validates_aggregate_backup_before_any_rollback(self):
        installer = (QUAL / "install-athena-alpha-m1-admin.sh").read_text(encoding="utf-8")
        self.assertIn('backup_manifest write', installer)
        self.assertIn('backup_manifest verify', installer)
        self.assertIn('"schema": 1, "entries": entries', installer)
        self.assertIn('os.fsync(directory_fd)', installer)
        self.assertIn('"$STATE/$key.owner-group"', installer)
        self.assertIn('stat -c \'%u:%g\' -- "$dst" > "$STATE/$key.owner-group"', installer)
        self.assertLess(installer.index('backup_manifest verify ||'), installer.index('rollback_one "$key" "$dst"'))

    def test_installer_embedded_python_blocks_compile_without_running(self):
        lines = (QUAL / "install-athena-alpha-m1-admin.sh").read_text(encoding="utf-8").splitlines()
        blocks, collecting = [], False
        current = []
        for line in lines:
            if not collecting and "<<'PY'" in line:
                collecting, current = True, []
            elif collecting and line == "PY":
                blocks.append("\n".join(current) + "\n")
                collecting = False
            elif collecting:
                current.append(line)
        self.assertTrue(blocks)
        for index, block in enumerate(blocks):
            compile(block, f"installer-embedded-{index}", "exec")


if __name__ == "__main__":
    unittest.main()
