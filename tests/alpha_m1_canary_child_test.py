#!/usr/bin/env python3
"""Static checks for the isolated Python program embedded in the canary."""

import builtins
import pathlib
import re
import symtable
import unittest


SOURCE = pathlib.Path(__file__).resolve().parents[1] / "qualification" / "athena-alpha-m1-systemd-canary.py"


def child_source():
    text = SOURCE.read_text(encoding="utf-8")
    match = re.search(r"^CHILD = r'''(.*?)'''\s*$", text, re.MULTILINE | re.DOTALL)
    if match is None:
        raise AssertionError("canary CHILD program was not found")
    return match.group(1)


def unresolved_globals(table):
    unresolved = set()
    for symbol in table.get_symbols():
        if (symbol.is_referenced() and symbol.is_global()
                and not symbol.is_assigned() and not symbol.is_imported()):
            if symbol.get_name() not in vars(builtins):
                unresolved.add(symbol.get_name())
    for nested in table.get_children():
        unresolved.update(unresolved_globals(nested))
    return unresolved


class CanaryChildTest(unittest.TestCase):
    def test_systemd_version_uses_systemctl_interface_and_resolved_tools(self):
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn('shutil.which("systemd-run", path=SYSTEMD_PATH)', source)
        self.assertIn('shutil.which("systemctl", path=SYSTEMD_PATH)', source)
        self.assertIn('[SYSTEMCTL, "--version"]', source)
        self.assertNotIn("/usr/bin/systemd\"", source)
        self.assertNotIn("/usr/bin/systemd'", source)

    def test_child_compiles_and_defines_all_referenced_globals(self):
        source = child_source()
        compile(source, str(SOURCE) + "::<CHILD>", "exec")
        table = symtable.symtable(source, str(SOURCE) + "::<CHILD>", "exec")
        self.assertEqual(set(), unresolved_globals(table))

    def test_fixture_path_is_supplied_as_child_argument(self):
        source = child_source()
        self.assertIn("ext4_parent", source)
        self.assertIn('os.path.join(ext4_parent, ".canary-" + nonce)', source)
        self.assertRegex(SOURCE.read_text(encoding="utf-8"), r"host_dev_mount,\s*EXT4_PARENT\]")

    def test_private_tmp_and_only_fixture_write_exception_are_probed(self):
        source = SOURCE.read_text(encoding="utf-8")
        child = child_source()
        self.assertIn('"--property=PrivateTmp=yes"', source)
        self.assertIn("if os.path.exists(os.path.join(host_tmp, \"host-marker\"))", child)
        self.assertIn('os.path.exists(os.path.join("/tmp", nonce + ".private"))', source)
        self.assertIn('os.path.exists(host_var_tmp_marker)', child)
        self.assertIn('private_var_tmp_file = "/var/tmp/" + nonce + ".private"', child)
        self.assertIn('os.path.exists(os.path.join("/var/tmp", nonce + ".private"))', source)
        self.assertIn('not os.path.isfile(host_var_tmp_marker)', source)
        self.assertIn('tempfile.mkstemp(', source)
        self.assertIn('dir="/var/tmp"', source)
        self.assertIn('os.unlink(host_var_tmp_marker)', source)
        self.assertIn('nonce, host_tmp, host_var_tmp_marker, host_mnt, host_net', source)
        self.assertIn('"--property=ReadWritePaths=/tmp " + EXT4_PARENT', source)
        for protected in ("/var/lib/athena-alpha-m1", "/var/lib/athena-alpha-m1/backups",
                          "/var/log/athena-alpha-m1"):
            self.assertIn(protected, child)
            self.assertIn(protected, source)
        self.assertIn('"FAIL: sandbox allowed write under " + protected', child)

    def test_preparation_timeout_and_cleanup_errors_are_handled(self):
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("Canary preparation/execution failed:", source)
        self.assertIn("except subprocess.TimeoutExpired:", source)
        self.assertIn('"--kill-whom=all"', source)
        self.assertIn("shutil.rmtree(host_tmp)", source)
        self.assertIn("Warning: could not remove canary temporary directory:", source)


if __name__ == "__main__":
    unittest.main()
