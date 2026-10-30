#!/usr/bin/env python3
"""Synthetic-only M34.3 security regression tests for the M34.1 collector."""
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
BACKUP = "var/lib/athena-alpha-m1/backups/run-20261021-061921"


class M343Regression(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="m34-3-regression-")
        self.base = Path(self.tmp.name)
        self.root = self.base / "source"
        self.out_parent = self.base / "output-parent"
        self.root.mkdir(mode=0o700); self.out_parent.mkdir(mode=0o700)
        for rel in SCOPES: (self.root / rel).mkdir(parents=True)
        (self.root / BACKUP).mkdir(parents=True)
        self.put(f"{M32}/inventory.json", b"{}\n")
        self.put(f"{M32}/SHA256SUMS", (hashlib.sha256(b"{}\n").hexdigest()+"  inventory.json\n").encode())
        self.put("usr/local/sbin/athena-alpha-m1-runner", b"runner")
        self.put("usr/local/sbin/gateway_support.py", b"support")
        self.put("etc/sudoers.d/athena-alpha-m1", b"sudoers")
        for d in sorted((x for x in self.root.rglob("*") if x.is_dir()), key=lambda x: len(x.parts), reverse=True): d.chmod(0o755)

    def tearDown(self): self.tmp.cleanup()

    def put(self, rel, data=b"fixture"):
        p=self.root/rel; p.parent.mkdir(parents=True,exist_ok=True); p.write_bytes(data); p.chmod(0o600); return p

    def run_it(self, out="result", **controls):
        env=os.environ.copy(); env["MONAD_M34_TEST_ROOT"]=str(self.root)
        env.update({k:str(v) for k,v in controls.items()})
        return subprocess.run(["bash",str(SCRIPT),str(self.out_parent/out)],env=env,text=True,capture_output=True,timeout=15)

    def inv(self, out="result"): return json.loads((self.out_parent/out/"inventory.json").read_text())

    def test_required_file_directory_and_required_directory_file_are_partial(self):
        f=self.root/"usr/local/sbin/athena-alpha-m1-runner"; f.unlink(); f.mkdir(mode=0o755); f.chmod(0o755)
        p=self.run_it(); self.assertEqual(p.returncode,1)
        inv=self.inv(); row=next(r for r in inv["results"] if r["path"].endswith("athena-alpha-m1-runner"))
        self.assertEqual((inv["completion"],row["status"],row["expected_type"]),("PARTIAL","TYPE_MISMATCH","file"))
        shutil.rmtree(self.out_parent/"result")
        d=self.root/BACKUP; shutil.rmtree(d); d.write_bytes(b"bad"); d.chmod(0o600)
        p=self.run_it(); self.assertEqual(p.returncode,1)
        inv=self.inv(); row=next(r for r in inv["results"] if r["path"].endswith("run-20261021-061921"))
        self.assertEqual((inv["completion"],row["expected_type"]),("PARTIAL","directory"))

    def test_final_and_intermediate_symlinks_are_not_followed(self):
        victim=self.put("outside",b"private")
        f=self.root/"usr/local/sbin/athena-alpha-m1-runner"; f.unlink(); f.symlink_to(victim)
        p=self.run_it(); self.assertEqual(p.returncode,1)
        self.assertIn("SYMLINK_NOT_FOLLOWED",{r["status"] for r in self.inv()["results"]})
        shutil.rmtree(self.out_parent/"result")
        d=self.root/"etc/sudoers.d"; d.rename(self.root/"etc/sudoers.saved"); d.symlink_to(self.root/"etc/sudoers.saved",target_is_directory=True)
        p=self.run_it(); self.assertEqual(p.returncode,1)
        self.assertIn("SYMLINK_NOT_FOLLOWED",{r["status"] for r in self.inv()["results"]})

    def test_missing_file_and_inaccessible_parent_are_partial(self):
        (self.root/"etc/sudoers.d/athena-alpha-m1").unlink()
        p=self.run_it(); self.assertEqual(p.returncode,1); self.assertEqual(self.inv()["completion"],"PARTIAL")
        shutil.rmtree(self.out_parent/"result")
        d=self.root/"root/athena-alpha-m1-stage"; d.chmod(0)
        try: p=self.run_it()
        finally: d.chmod(0o755)
        self.assertEqual(p.returncode,1); self.assertNotEqual(self.inv()["completion"],"COMPLETE")

    def test_mount_boundary_simulation_partial_and_unavailable_mount_id_blocked(self):
        p=self.run_it(MONAD_M34_TEST_BIND_MOUNT="/usr/local/sbin/athena-alpha-m1-runner")
        self.assertEqual(p.returncode,1)
        inv=self.inv(); self.assertEqual(inv["completion"],"PARTIAL")
        self.assertIn("MOUNT_BOUNDARY",{r["status"] for r in inv["results"]})
        shutil.rmtree(self.out_parent/"result")
        p=self.run_it(MONAD_M34_TEST_MOUNT_ID_UNAVAILABLE="1")
        self.assertEqual(p.returncode,2); self.assertFalse((self.out_parent/"result").exists())

    def test_mount_verification_error_is_blocked_and_manifested(self):
        p=self.run_it(MONAD_M34_TEST_MOUNT_ID_ERROR="/usr/local/sbin/athena-alpha-m1-runner")
        self.assertEqual(p.returncode,2); inv=self.inv()
        self.assertEqual(inv["completion"],"BLOCKED")
        self.assertIn("MOUNT_ID_UNAVAILABLE",{r["status"] for r in inv["results"]})

    def test_exact_valid_marker_captured_unexpected_marker_hash_only(self):
        approved=self.put(f"{BACKUP}/runner.absent",b"marker")
        hostile=self.put(f"{BACKUP}/private.absent",b"secret payload")
        p=self.run_it(); self.assertEqual(p.returncode,1); inv=self.inv()
        rows={r["path"]:r for r in inv["results"]}
        self.assertEqual(rows["/"+BACKUP+"/runner.absent"]["content_status"],"captured_explicit_name_allowlist")
        bad=rows["/"+BACKUP+"/private.absent"]
        self.assertEqual(bad["content_status"],"sha256_only_unapproved_marker")
        self.assertIn("UNEXPECTED_BACKUP_MARKER",{x["status"] for x in inv["issues"]})
        captures=[x.read_bytes() for x in (self.out_parent/"result/captured").iterdir()]
        self.assertIn(approved.read_bytes(),captures); self.assertNotIn(hostile.read_bytes(),captures)

    def test_marker_name_hostile_is_never_copied(self):
        payload=self.put(f"{BACKUP}/..secret.absent",b"hostile-name-payload")
        p=self.run_it(); self.assertEqual(p.returncode,1)
        self.assertFalse(any(x.read_bytes()==payload.read_bytes() for x in (self.out_parent/"result/captured").iterdir()))

    def test_hash_mutation_is_reported_and_manifest_hashes_are_valid(self):
        p=self.run_it(MONAD_M34_TEST_MUTATE_FILE="/usr/local/sbin/athena-alpha-m1-runner")
        self.assertEqual(p.returncode,1); inv=self.inv()
        self.assertEqual(inv["completion"],"PARTIAL")
        self.assertIn("CHANGED_DURING_ACQUISITION",{r["status"] for r in inv["results"]})
        sums=(self.out_parent/"result/SHA256SUMS").read_text().splitlines()
        for line in sums:
            digest,name=line.split("  ",1)
            self.assertEqual(hashlib.sha256((self.out_parent/"result"/name).read_bytes()).hexdigest(),digest)

    def test_existing_output_refused_and_sources_unchanged(self):
        before={p.relative_to(self.root):hashlib.sha256(p.read_bytes()).hexdigest() for p in self.root.rglob("*") if p.is_file()}
        (self.out_parent/"exists").mkdir(); (self.out_parent/"exists/sentinel").write_text("keep")
        self.assertNotEqual(self.run_it("exists").returncode,0)
        self.assertEqual((self.out_parent/"exists/sentinel").read_text(),"keep")
        after={p.relative_to(self.root):hashlib.sha256(p.read_bytes()).hexdigest() for p in self.root.rglob("*") if p.is_file()}
        self.assertEqual(before,after)

    def test_embedded_python_and_bash_syntax(self):
        source=SCRIPT.read_text(); embedded=source.split("<<'PY'\n",1)[1].rsplit("\nPY",1)[0]
        compile(embedded,str(SCRIPT),"exec")
        p=subprocess.run(["bash","-n",str(SCRIPT)],capture_output=True,text=True)
        self.assertEqual(p.returncode,0,p.stderr)

    def test_completion_classification_complete_partial_blocked(self):
        p=self.run_it(); self.assertEqual(p.returncode,0); self.assertEqual(self.inv()["completion"],"COMPLETE")
        shutil.rmtree(self.out_parent/"result")
        (self.root/"etc/sudoers.d/athena-alpha-m1").unlink()
        p=self.run_it(); self.assertEqual(p.returncode,1); self.assertEqual(self.inv()["completion"],"PARTIAL")
        shutil.rmtree(self.out_parent/"result")
        p=self.run_it(MONAD_M34_TEST_INTERRUPT_AFTER_FIRST_SCOPE="1")
        self.assertEqual(p.returncode,2); self.assertEqual(self.inv()["completion"],"BLOCKED")


if __name__=="__main__": unittest.main(verbosity=2)
