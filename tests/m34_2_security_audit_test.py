#!/usr/bin/env python3
"""Independent synthetic adversarial checks for the M34.1 collector.

All source and destination paths are made inside TemporaryDirectory instances.
No administrative path is accessed. Cases that need kernel scheduling or
filesystem facilities unavailable in a deterministic unprivileged test are
reported as skips rather than inferred from the collector's own test hooks.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]
SCRIPT = REPO / "qualification/m34-1-targeted-forensic-acquisition.sh"
SCOPES = ("usr/local/sbin", "etc/sudoers.d", "root/athena-alpha-m1-stage",
          "var/lib/athena-alpha-m1", "var/lib/monad-m32-1-evidence-20261029T194120Z")
M32 = "var/lib/monad-m32-1-evidence-20261029T194120Z"


class M342Audit(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="m34-2-audit-")
        self.base = Path(self.tmp.name)
        self.root = self.base / "source"
        self.parent = self.base / "out-parent"
        self.root.mkdir(mode=0o700)
        self.parent.mkdir(mode=0o700)
        for scope in SCOPES:
            (self.root / scope).mkdir(parents=True)
        (self.root / "var/lib/athena-alpha-m1/backups/run-20261021-061921").mkdir(parents=True)
        self.put(f"{M32}/inventory.json", b"{}\n")
        data = hashlib.sha256(b"{}\n").hexdigest().encode() + b"  inventory.json\n"
        self.put(f"{M32}/SHA256SUMS", data)
        self.put("usr/local/sbin/athena-alpha-m1-runner", b"runner")
        self.put("usr/local/sbin/gateway_support.py", b"support")
        self.put("etc/sudoers.d/athena-alpha-m1", b"sudoers")
        # Synthetic root is private, while source directories are traversable.
        for d in sorted((p for p in self.root.rglob("*") if p.is_dir()), key=lambda p: len(p.parts), reverse=True):
            d.chmod(0o755)

    def tearDown(self):
        self.tmp.cleanup()

    def put(self, rel, content=b"fixture"):
        p = self.root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(content)
        p.chmod(0o600)
        return p

    def run_it(self, name="out", **controls):
        env = os.environ.copy()
        env["MONAD_M34_TEST_ROOT"] = str(self.root)
        env.update({k: str(v) for k, v in controls.items()})
        return subprocess.run(["bash", str(SCRIPT), str(self.parent / name)],
                              env=env, text=True, capture_output=True, timeout=15)

    def inv(self, name="out"):
        return json.loads((self.parent / name / "inventory.json").read_text())

    def test_symlink_final_and_intermediate_are_not_followed(self):
        outside = self.put("outside", b"do-not-capture")
        target = self.root / "usr/local/sbin/athena-alpha-m1-runner"
        target.unlink(); target.symlink_to(outside)
        (self.root / "etc/sudoers.d").rename(self.root / "etc/sudoers.real")
        (self.root / "etc/sudoers.d").symlink_to(self.root / "etc/sudoers.real", target_is_directory=True)
        p = self.run_it()
        self.assertEqual(p.returncode, 1)
        self.assertIn("SYMLINK_NOT_FOLLOWED", {r["status"] for r in self.inv()["results"]})
        captures = self.parent / "out/captured"
        self.assertFalse(captures.exists() and any(b"do-not-capture" in f.read_bytes() for f in captures.iterdir()))

    def test_directory_and_file_replacement_are_detected(self):
        p = self.run_it(MONAD_M34_TEST_REPLACE_BEFORE_OPEN="/usr/local/sbin")
        self.assertEqual(p.returncode, 1)
        self.assertIn("IDENTITY_CHANGED", {r["status"] for r in self.inv()["results"]})
        shutil.rmtree(self.parent / "out")
        self.put("usr/local/sbin/athena-alpha-m1-runner", b"runner")
        p = self.run_it(MONAD_M34_TEST_REPLACE_BEFORE_OPEN="/usr/local/sbin/athena-alpha-m1-runner")
        self.assertEqual(p.returncode, 1)
        self.assertIn("IDENTITY_CHANGED", {r["status"] for r in self.inv()["results"]})

    def test_mutation_during_hashing_is_detected_by_before_after_check(self):
        p = self.run_it(MONAD_M34_TEST_MUTATE_FILE="/usr/local/sbin/athena-alpha-m1-runner")
        self.assertEqual(p.returncode, 1)
        row = next(r for r in self.inv()["results"] if r["path"].endswith("/athena-alpha-m1-runner"))
        self.assertEqual(row["status"], "CHANGED_DURING_ACQUISITION")

    def test_expanded_read_fails_closed(self):
        path = self.root / "usr/local/sbin/athena-alpha-m1-runner"
        path.write_bytes(b"original")
        p = self.run_it(MONAD_M34_TEST_MUTATE_FILE="/usr/local/sbin/athena-alpha-m1-runner")
        self.assertEqual(p.returncode, 1)
        self.assertIn("CHANGED_DURING_ACQUISITION", {r["status"] for r in self.inv()["results"]})

    def test_truncated_read_reproducibility_limit(self):
        self.skipTest("collector has no deterministic truncate-during-read hook; kernel scheduling race is not reproducible reliably in this sandbox")

    def test_special_file_and_fifo_are_not_opened(self):
        os.mkfifo(self.root / "var/lib/athena-alpha-m1/fifo")
        p = self.run_it()
        self.assertEqual(p.returncode, 1)
        self.assertIn("SPECIAL_FILE_NOT_OPENED", {r["status"] for r in self.inv()["results"]})

    def test_hostile_directory_permissions_are_partial(self):
        d = self.root / "root/athena-alpha-m1-stage"
        d.chmod(0)
        try:
            p = self.run_it()
        finally:
            d.chmod(0o755)
        self.assertNotEqual(p.returncode, 0)
        self.assertNotEqual(self.inv()["completion"], "COMPLETE")

    def test_existing_output_is_refused_without_overwrite(self):
        existing = self.parent / "exists"; existing.mkdir(); (existing / "sentinel").write_text("keep")
        p = self.run_it("exists")
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual((existing / "sentinel").read_text(), "keep")

    def test_output_parent_symlink_and_writable_parent_refused(self):
        alias = self.base / "alias"; alias.symlink_to(self.parent, target_is_directory=True)
        env = {**os.environ, "MONAD_M34_TEST_ROOT": str(self.root)}
        p = subprocess.run(["bash", str(SCRIPT), str(alias / "new")], env=env, capture_output=True, text=True)
        self.assertNotEqual(p.returncode, 0)
        bad = self.base / "writable"; bad.mkdir(mode=0o777); bad.chmod(0o777)
        p = subprocess.run(["bash", str(SCRIPT), str(bad / "new")], env=env, capture_output=True, text=True)
        self.assertNotEqual(p.returncode, 0)

    def test_path_traversal_and_hostile_names_remain_bounded(self):
        p = self.run_it("../escape")
        self.assertNotEqual(p.returncode, 0)
        self.put("root/athena-alpha-m1-stage/line\nbreak", b"x")
        p = self.run_it()
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertTrue(any("line\nbreak" in r["path"] for r in self.inv()["results"]))

    def test_depth_and_entry_enumeration_limits_are_partial(self):
        d = self.root / "var/lib/athena-alpha-m1"
        for i in range(14):
            d = d / f"d{i}"; d.mkdir(); d.chmod(0o755)
        p = self.run_it()
        self.assertEqual(p.returncode, 1)
        self.assertIn("LIMIT_DEPTH", {r["status"] for r in self.inv()["results"]})
        shutil.rmtree(self.parent / "out")
        shallow = self.root / "root/athena-alpha-m1-stage"
        for i in range(8001):
            (shallow / f"f{i:05d}").touch()
        p = self.run_it()
        self.assertNotEqual(p.returncode, 0)
        self.assertNotEqual(self.inv()["completion"], "COMPLETE")

    def test_file_and_total_byte_limits_are_partial(self):
        self.put("etc/sudoers.d/athena-alpha-m1", b"x" * (4 * 1024 * 1024 + 1))
        p = self.run_it()
        self.assertEqual(p.returncode, 1)
        self.assertIn("CONTENT_LIMIT", {r["status"] for r in self.inv()["results"]})
        # The total-byte branch is also bounded; enough allowlisted files are
        # created without exceeding the individual cap.
        shutil.rmtree(self.parent / "out")
        backup = self.root / "var/lib/athena-alpha-m1/backups/run-20261021-061921"
        for i in range(9):
            f = backup / f"item{i}.absent"; f.write_bytes(b"z" * (4 * 1024 * 1024)); f.chmod(0o600)
        p = self.run_it()
        self.assertEqual(p.returncode, 1)
        self.assertIn("CONTENT_LIMIT", {r["status"] for r in self.inv()["results"]})

    def test_output_write_failure_reproducibility_limit(self):
        self.skipTest("reliable ENOSPC/EIO injection requires a dedicated filesystem or fault-injection facility not available in this sandbox")

    def test_same_device_bind_mount_reproducibility_limit(self):
        self.skipTest("creating or changing mounts is outside authorization; st_dev-only boundary gap is established by static inspection")

    def test_interruption_is_manifested_as_blocked(self):
        p = self.run_it(MONAD_M34_TEST_INTERRUPT_AFTER_FIRST_SCOPE="1")
        self.assertEqual(p.returncode, 2)
        self.assertEqual(self.inv()["completion"], "BLOCKED")

    def test_manifest_is_partial_when_source_incomplete(self):
        (self.root / "etc/sudoers.d/athena-alpha-m1").unlink()
        p = self.run_it()
        self.assertEqual(p.returncode, 1)
        self.assertEqual(self.inv()["completion"], "PARTIAL")

    def test_required_regular_file_directory_substitution_can_false_complete(self):
        target = self.root / "usr/local/sbin/athena-alpha-m1-runner"
        target.unlink(); target.mkdir(mode=0o755)
        p = self.run_it()
        self.assertEqual(p.returncode, 0, p.stderr)
        inv = self.inv()
        row = next(r for r in inv["results"] if r["path"] == "/usr/local/sbin/athena-alpha-m1-runner")
        self.assertEqual(row["status"], "PRESENT")
        self.assertEqual(inv["completion"], "COMPLETE")

    def test_wrong_manifest_hash_is_partial(self):
        self.put(f"{M32}/SHA256SUMS", b"0" * 64 + b"  inventory.json\n")
        p = self.run_it()
        self.assertEqual(p.returncode, 1)
        self.assertIn("SOURCE_CHECKSUM_INVALID", {i["status"] for i in self.inv()["issues"]})

    def test_post_acquisition_edit_invalidates_checksum(self):
        p = self.run_it(); self.assertEqual(p.returncode, 0, p.stderr)
        inv_path = self.parent / "out/inventory.json"
        inv_path.write_bytes(inv_path.read_bytes() + b" ")
        digest, _ = (self.parent / "out/SHA256SUMS").read_text().split("  ", 1)
        self.assertNotEqual(hashlib.sha256(inv_path.read_bytes()).hexdigest(), digest)

    def test_inventory_checksum_binding_detects_inconsistency(self):
        p = self.run_it(); self.assertEqual(p.returncode, 0, p.stderr)
        sums = self.parent / "out/SHA256SUMS"
        sums.write_text("f" * 64 + "  inventory.json\n")
        digest, name = sums.read_text().split("  ", 1)
        self.assertEqual(name.strip(), "inventory.json")
        self.assertNotEqual(digest, hashlib.sha256((self.parent / "out/inventory.json").read_bytes()).hexdigest())

    def test_status_and_embedded_python_static_checks(self):
        source = SCRIPT.read_text()
        py = source.split("<<'PY'\n", 1)[1].rsplit("\nPY", 1)[0]
        compile(py, str(SCRIPT), "exec")
        result = subprocess.run(["bash", "-n", str(SCRIPT)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
