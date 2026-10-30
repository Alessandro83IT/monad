#!/usr/bin/env python3
"""Synthetic-only regression tests for the M34.1 shell/Python collector."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]
SCRIPT = REPO / "qualification/m34-1-targeted-forensic-acquisition.sh"
SOURCE = SCRIPT.read_text()
PYTHON = SOURCE.split("<<'PY'\n", 1)[1].rsplit("\nPY", 1)[0]
SCOPES = ("usr/local/sbin", "etc/sudoers.d", "root/athena-alpha-m1-stage",
          "var/lib/athena-alpha-m1", "var/lib/monad-m32-1-evidence-20261029T194120Z")


class AcquisitionTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="m34-1-")
        self.base = Path(self.tmp.name)
        self.root = self.base / "synthetic-root"
        self.root.mkdir(mode=0o700)
        self.parent = self.base / "outputs"
        self.parent.mkdir(mode=0o700)
        for name in SCOPES:
            (self.root / name).mkdir(parents=True)
        (self.root / "var/lib/athena-alpha-m1/backups/run-20261021-061921").mkdir(parents=True)
        self.put("var/lib/monad-m32-1-evidence-20261029T194120Z/inventory.json", b"{}\n")
        sums = hashlib.sha256(b"{}\n").hexdigest().encode() + b"  inventory.json\n"
        self.put("var/lib/monad-m32-1-evidence-20261029T194120Z/SHA256SUMS", sums)
        self.put("usr/local/sbin/athena-alpha-m1-runner", b"runner")
        self.put("usr/local/sbin/gateway_support.py", b"support")
        self.put("etc/sudoers.d/athena-alpha-m1", b"sudoers")
        for directory in sorted((p for p in self.root.rglob("*") if p.is_dir()), key=lambda p: len(p.parts), reverse=True):
            directory.chmod(0o755)

    def tearDown(self):
        self.tmp.cleanup()

    def put(self, rel, data=b"fixture"):
        p = self.root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
        p.chmod(0o600)
        return p

    def run_collector(self, output="out", **env):
        e = os.environ.copy()
        e["MONAD_M34_TEST_ROOT"] = str(self.root)
        e.update({k: str(v) for k, v in env.items()})
        return subprocess.run(["bash", str(SCRIPT), str(self.parent / output)], env=e,
                              text=True, capture_output=True, timeout=10)

    def inventory(self, output="out"):
        return json.loads((self.parent / output / "inventory.json").read_text())

    def test_embedded_python_compiles(self):
        compile(PYTHON, str(SCRIPT), "exec")
        self.assertIn("def validate_configuration()", PYTHON)
        self.assertIn("required path outside configured roots", PYTHON.lower())

    def test_valid_acquisition_and_checksum_permissions(self):
        self.put("usr/local/sbin/athena-alpha-m1-runner", b"runner bytes")
        p = self.run_collector()
        self.assertEqual(p.returncode, 0, p.stderr)
        inv = self.inventory()
        self.assertEqual(inv["completion"], "COMPLETE")
        self.assertEqual(inv["counts"]["bytes_read"], sum(r["metadata_before"]["size"] for r in inv["results"] if r.get("content_status") == "captured_explicit_name_allowlist"))
        row = next(r for r in inv["results"] if r["path"] == "/usr/local/sbin/athena-alpha-m1-runner")
        captured = self.parent / "out" / row["evidence_file"]
        self.assertEqual(captured.read_bytes(), b"runner bytes")
        sums = (self.parent / "out/SHA256SUMS").read_text().splitlines()
        for row in sums:
            digest, rel = row.split("  ", 1)
            self.assertEqual(hashlib.sha256((self.parent / "out" / rel).read_bytes()).hexdigest(), digest)
        for path in (self.parent / "out", self.parent / "out/inventory.json", self.parent / "out/SHA256SUMS", captured):
            self.assertEqual(path.stat().st_mode & 0o777, 0o700 if path.is_dir() else 0o600)

    def test_absent_scope_is_partial_not_absent_evidence(self):
        shutil.rmtree(self.root / "etc/sudoers.d")
        p = self.run_collector()
        self.assertEqual(p.returncode, 1)
        inv = self.inventory()
        self.assertEqual(inv["completion"], "PARTIAL")
        self.assertTrue(any(x.get("status") == "REQUIRED_EVIDENCE_UNAVAILABLE" for x in inv["issues"]))
        self.assertFalse(any(r.get("path") == "/etc/sudoers.d/athena-alpha-m1" and r.get("status") == "REQUIRED_ABSENT" for r in inv["results"]))

    def test_manifest_marks_missing_required_leaf(self):
        (self.root / "etc/sudoers.d/athena-alpha-m1").unlink()
        p = self.run_collector()
        self.assertEqual(p.returncode, 1)
        leaf = next(r for r in self.inventory()["results"] if r["path"] == "/etc/sudoers.d/athena-alpha-m1")
        self.assertEqual(leaf["status"], "REQUIRED_ABSENT")

    def test_required_file_replaced_by_directory_is_partial_with_reason(self):
        target = self.root / "usr/local/sbin/athena-alpha-m1-runner"
        target.unlink(); target.mkdir(mode=0o755)
        p = self.run_collector()
        self.assertEqual(p.returncode, 1)
        inv = self.inventory()
        row = next(r for r in inv["results"] if r["path"] == "/usr/local/sbin/athena-alpha-m1-runner")
        self.assertEqual(row["status"], "TYPE_MISMATCH")
        self.assertEqual(row["expected_type"], "file")
        self.assertIn("REQUIRED_TYPE_MISMATCH", {i["status"] for i in inv["issues"]})
        self.assertEqual(inv["completion"], "PARTIAL")

    def test_required_backup_directory_replaced_by_file_is_partial(self):
        target = self.root / "var/lib/athena-alpha-m1/backups/run-20261021-061921"
        shutil.rmtree(target); target.write_bytes(b"wrong type"); target.chmod(0o600)
        p = self.run_collector()
        self.assertEqual(p.returncode, 1)
        inv = self.inventory()
        row = next(r for r in inv["results"] if r["path"] == "/var/lib/athena-alpha-m1/backups/run-20261021-061921")
        self.assertEqual(row["status"], "TYPE_MISMATCH")
        self.assertEqual(row["expected_type"], "directory")
        self.assertEqual(inv["completion"], "PARTIAL")

    def test_m32_source_checksum_mismatch_is_partial(self):
        (self.root / "var/lib/monad-m32-1-evidence-20261029T194120Z/SHA256SUMS").write_text("0" * 64 + "  inventory.json\n")
        p = self.run_collector()
        self.assertEqual(p.returncode, 1)
        self.assertTrue(any(i["status"] == "SOURCE_CHECKSUM_INVALID" for i in self.inventory()["issues"]))

    def test_leaf_symlink_and_intermediate_symlink_are_never_followed(self):
        victim = self.put("outside", b"secret")
        (self.root / "etc/sudoers.d/athena-alpha-m1").unlink()
        os.symlink(victim, self.root / "etc/sudoers.d/athena-alpha-m1")
        (self.root / "root/athena-alpha-m1-stage/link").symlink_to(self.root / "outside")
        p = self.run_collector()
        self.assertEqual(p.returncode, 1)
        statuses = {r["status"] for r in self.inventory()["results"]}
        self.assertIn("SYMLINK_NOT_FOLLOWED", statuses)
        self.assertNotIn(b"secret", b"".join(x.read_bytes() for x in (self.parent / "out/captured").glob("*")) if (self.parent / "out/captured").exists() else b"")

    def test_fifo_is_special_and_not_opened(self):
        os.mkfifo(self.root / "var/lib/athena-alpha-m1/odd")
        p = self.run_collector()
        self.assertEqual(p.returncode, 1)
        self.assertIn("SPECIAL_FILE_NOT_OPENED", {r["status"] for r in self.inventory()["results"]})

    def test_hardlink_and_unsafe_permissions_are_anomalies(self):
        runner = self.root / "usr/local/sbin/athena-alpha-m1-runner"
        alias = self.root / "usr/local/sbin/runner-hardlink"
        os.link(runner, alias)
        (self.root / "usr/local/sbin/gateway_support.py").chmod(0o666)
        p = self.run_collector()
        self.assertEqual(p.returncode, 1)
        rows = {r["path"]: r["status"] for r in self.inventory()["results"]}
        self.assertEqual(rows["/usr/local/sbin/athena-alpha-m1-runner"], "HARDLINK_ANOMALY")
        self.assertEqual(rows["/usr/local/sbin/runner-hardlink"], "HARDLINK_ANOMALY")
        self.assertEqual(rows["/usr/local/sbin/gateway_support.py"], "UNSAFE_PERMISSIONS")

    def test_file_replacement_race_is_detected(self):
        self.put("usr/local/sbin/athena-alpha-m1-runner", b"before")
        p = self.run_collector(MONAD_M34_TEST_REPLACE_BEFORE_OPEN="/usr/local/sbin/athena-alpha-m1-runner")
        self.assertEqual(p.returncode, 1)
        self.assertIn("IDENTITY_CHANGED", {r["status"] for r in self.inventory()["results"]})

    def test_directory_replacement_race_is_detected(self):
        p = self.run_collector(MONAD_M34_TEST_REPLACE_BEFORE_OPEN="/usr/local/sbin")
        self.assertEqual(p.returncode, 1)
        self.assertIn("IDENTITY_CHANGED", {r["status"] for r in self.inventory()["results"]})

    def test_name_replacement_after_open_is_detected(self):
        p = self.run_collector(MONAD_M34_TEST_REPLACE_AFTER_OPEN="/usr/local/sbin/athena-alpha-m1-runner")
        self.assertEqual(p.returncode, 1)
        self.assertIn("PATH_REPLACED_DURING_ACQUISITION", {r["status"] for r in self.inventory()["results"]})

    def test_mutations_during_read_and_directory_walk_are_detected(self):
        self.put("usr/local/sbin/athena-alpha-m1-runner", b"before")
        p = self.run_collector(MONAD_M34_TEST_MUTATE_FILE="/usr/local/sbin/athena-alpha-m1-runner")
        self.assertEqual(p.returncode, 1)
        self.assertIn("CHANGED_DURING_ACQUISITION", {r["status"] for r in self.inventory()["results"]})
        shutil.rmtree(self.parent / "out")
        p = self.run_collector(MONAD_M34_TEST_MUTATE_DIRECTORY="/usr/local/sbin")
        self.assertEqual(p.returncode, 1)
        self.assertIn("CHANGED_DURING_ACQUISITION", {r["status"] for r in self.inventory()["results"]})

    def test_file_too_large_and_depth_limit(self):
        self.put("etc/sudoers.d/athena-alpha-m1", b"x" * (4 * 1024 * 1024 + 1))
        deep = self.root / "var/lib/athena-alpha-m1"
        for i in range(14):
            deep = deep / ("d" + str(i)); deep.mkdir(); deep.chmod(0o755)
        p = self.run_collector()
        self.assertEqual(p.returncode, 1)
        statuses = {r["status"] for r in self.inventory()["results"]}
        self.assertIn("CONTENT_LIMIT", statuses)
        self.assertIn("LIMIT_DEPTH", statuses)

    def test_permission_error_is_recorded_without_escalation(self):
        pth = self.root / "root/athena-alpha-m1-stage"
        (pth / "unlisted.log").write_bytes(b"private")
        pth.chmod(0)
        p = self.run_collector()
        pth.chmod(0o700)
        self.assertEqual(p.returncode, 1)
        self.assertTrue(any(r.get("status") in ("ENUMERATION_ERROR", "OPEN_ERROR", "PERMISSION_ERROR") for r in self.inventory()["results"]))

    def test_entry_budget_is_explicit_and_partial(self):
        for i in range(4002):
            (self.root / "root/athena-alpha-m1-stage" / ("f%04d" % i)).touch()
        p = self.run_collector()
        self.assertEqual(p.returncode, 1)
        self.assertTrue(any(i.get("status") == "LIMIT_ENTRIES" for i in self.inventory()["issues"]))

    def test_controlled_interruption_is_blocked_and_manifested(self):
        p = self.run_collector(MONAD_M34_TEST_INTERRUPT_AFTER_FIRST_SCOPE="1")
        self.assertEqual(p.returncode, 2)
        inv = self.inventory()
        self.assertEqual(inv["completion"], "BLOCKED")
        self.assertTrue(any(i["status"] == "CONTROLLED_INTERRUPTION" for i in inv["issues"]))

    def test_existing_output_and_unsafe_output_parent_refused(self):
        (self.parent / "exists").mkdir()
        p = self.run_collector("exists")
        self.assertNotEqual(p.returncode, 0)
        unsafe = self.base / "unsafe"
        unsafe.mkdir(mode=0o755)
        e = os.environ.copy(); e["MONAD_M34_TEST_ROOT"] = str(self.root)
        p = subprocess.run(["bash", str(SCRIPT), str(unsafe / "new")], env=e, text=True, capture_output=True)
        self.assertNotEqual(p.returncode, 0)

    def test_symlink_in_output_parent_is_refused(self):
        alias = self.base / "parent-link"
        alias.symlink_to(self.parent, target_is_directory=True)
        p = subprocess.run(["bash", str(SCRIPT), str(alias / "new")],
                           env={**os.environ, "MONAD_M34_TEST_ROOT": str(self.root)}, text=True, capture_output=True)
        self.assertEqual(p.returncode, 2)

    def test_output_outside_synthetic_source_is_required(self):
        p = subprocess.run(["bash", str(SCRIPT), str(self.root / "output")],
                           env={**os.environ, "MONAD_M34_TEST_ROOT": str(self.root)}, text=True, capture_output=True)
        self.assertNotEqual(p.returncode, 0)

    def test_intermediate_symlink_is_blocked(self):
        shutil.rmtree(self.root / "etc/sudoers.d")
        (self.root / "etc/sudoers.d").symlink_to(self.root / "outside")
        p = self.run_collector()
        self.assertEqual(p.returncode, 1)
        self.assertIn("SYMLINK_NOT_FOLLOWED", {r["status"] for r in self.inventory()["results"]})

    def test_manifest_has_every_result_and_checksum_covers_inventory(self):
        p = self.run_collector()
        self.assertEqual(p.returncode, 0, p.stderr)
        inv = self.inventory()
        self.assertEqual(len(inv["results"]), len({r["path"] for r in inv["results"]}))
        sums = (self.parent / "out/SHA256SUMS").read_text()
        self.assertEqual(sums.split()[0], hashlib.sha256((self.parent / "out/inventory.json").read_bytes()).hexdigest())

    def test_scope_limits_and_content_allowlist_are_explicit(self):
        self.put("usr/local/sbin/unlisted.log", b"do not copy")
        payload = self.put("var/lib/athena-alpha-m1/backups/run-20261021-061921/payload-unknown.bin", b"payload bytes")
        self.put("var/lib/athena-alpha-m1/ext4-fixture/private-marker", b"fixture bytes")
        (self.root / "var/lib/athena-alpha-m1/ext4-fixture").chmod(0o755)
        p = self.run_collector()
        self.assertEqual(p.returncode, 0, p.stderr)
        rows = {r["path"]: r for r in self.inventory()["results"]}
        self.assertEqual(rows["/usr/local/sbin/unlisted.log"]["content_status"], "metadata_only")
        backup_row = rows["/var/lib/athena-alpha-m1/backups/run-20261021-061921/payload-unknown.bin"]
        self.assertEqual(backup_row["content_status"], "sha256_only_backup_payload")
        self.assertEqual(backup_row["sha256"], hashlib.sha256(payload.read_bytes()).hexdigest())
        self.assertNotIn("/var/lib/athena-alpha-m1/ext4-fixture/private-marker", rows)
        self.assertEqual(rows["/var/lib/athena-alpha-m1/ext4-fixture"]["status"], "PRESENT_POLICY_PRUNED")
        self.assertFalse(any(x.read_bytes() == b"do not copy" for x in (self.parent / "out/captured").glob("*") ))

    def test_unapproved_marker_is_hash_only_and_partial(self):
        marker = self.put("var/lib/athena-alpha-m1/backups/run-20261021-061921/hostile.absent", b"secret payload")
        p = self.run_collector()
        self.assertEqual(p.returncode, 1)
        inv = self.inventory()
        row = next(r for r in inv["results"] if r["path"].endswith("/hostile.absent"))
        self.assertEqual(row["content_status"], "sha256_only_unapproved_marker")
        self.assertIn("UNEXPECTED_BACKUP_MARKER", {i["status"] for i in inv["issues"]})
        self.assertFalse(any(f.read_bytes() == marker.read_bytes() for f in (self.parent / "out/captured").glob("*")))

    def test_mount_id_failure_blocks_before_output(self):
        p = self.run_collector(MONAD_M34_TEST_MOUNT_ID_UNAVAILABLE="1")
        self.assertEqual(p.returncode, 2)
        self.assertFalse((self.parent / "out").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
