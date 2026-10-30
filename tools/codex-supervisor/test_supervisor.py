import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
import tempfile
import unittest
from unittest import mock

import supervisor as s


FAKE = '''#!/usr/bin/env python3
import json, os, pathlib, subprocess, sys, time
if sys.argv[1:] == ["--version"]:
 if os.environ.get("FAKE_VERSION_MODE") == "child":
  p=subprocess.Popen([sys.executable,"-c","import time; time.sleep(30)"])
  pathlib.Path(os.environ["CHILD_PID"]).write_text(str(p.pid)); time.sleep(30)
 print(os.environ.get("FAKE_VERSION", "codex-cli 0.164.0")); sys.exit(int(os.environ.get("FAKE_VERSION_EXIT", "0")))
if sys.argv[1] != "exec" or sys.argv[2:3] not in (["--approve-for-me"], ["--sandbox"]): sys.exit(12)
if "ARGS_FILE" in os.environ: pathlib.Path(os.environ["ARGS_FILE"]).write_text(json.dumps(sys.argv[1:]))
mode=os.environ.get("FAKE_MODE", "ok")
if mode in ("arg0-link", "arg0-link-wrong-target"):
 d=pathlib.Path(os.environ["CODEX_HOME"])/"tmp"/"arg0-linux"/"codex-argAbc123"; d.mkdir(parents=True)
 target=os.environ["FAKE_ARG0_TARGET"] if mode == "arg0-link" else os.environ["FAKE_ARG0_WRONG_TARGET"]
 for name in ("codex", "applypatch", "apply_patch", "codex-linux-sandbox"): (d/name).symlink_to(target)
if mode != "quiet": print("synthetic codex activity")
if mode == "sleep": time.sleep(20)
if mode == "child":
 p=subprocess.Popen([sys.executable,"-c","import time; time.sleep(30)"])
 pathlib.Path(os.environ["CHILD_PID"]).write_text(str(p.pid))
 time.sleep(20)
if mode == "mutate": pathlib.Path(os.environ["TARGET"]).write_text("changed")
if mode == "protected": pathlib.Path(os.environ["TARGET"]).write_text("changed")
if mode == "branch": subprocess.run(["git","checkout","-qb","fake-branch"],check=True)
if mode == "plan-mutate": pathlib.Path(os.environ["PLANPATH"]).write_text("changed plan bytes")
if mode == "nested-output":
 p=pathlib.Path(os.environ["CODEX_HOME"])/"nested"; p.mkdir(); (p/"large.bin").write_bytes(b"x"*1000)
if mode == "many-changes":
 p=pathlib.Path("allowed"); p.mkdir(exist_ok=True); (p/"one").write_text("1"); (p/"two").write_text("2")
if "--output-last-message" in sys.argv:
 out=pathlib.Path(sys.argv[sys.argv.index("--output-last-message")+1])
 if mode == "secret": out.write_text("SIMULATED_SECRET_9fc1")
 elif mode != "no-report": out.write_text("synthetic report")
sys.exit(7 if mode == "fail" else 0)
'''


class SupervisorTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.base = self.root / ".codex-supervisor"
        self.plan = self.root / "milestones.json"
        self.fake = self.root / "codex-fake"
        self.fake.write_text("#!" + sys.executable + "\n" + FAKE.split("\n", 1)[1]); self.fake.chmod(0o700)
        (self.root/"fixture_test.py").write_text("import unittest\nclass TestFixture(unittest.TestCase):\n def test_pass(self): self.assertTrue(True)\n")
        (self.root/"failing_test.py").write_text("import unittest\nclass TestFixture(unittest.TestCase):\n def test_fail(self): self.fail('synthetic failure')\n")
        (self.root/"slow_test.py").write_text("import unittest, time\nclass TestFixture(unittest.TestCase):\n def test_slow(self): time.sleep(20)\n")
        (self.root/"descendant_test.py").write_text("import pathlib, subprocess, sys, unittest\nclass TestFixture(unittest.TestCase):\n def test_child(self):\n  p=subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)']); pathlib.Path('descendant.pid').write_text(str(p.pid))\n")
        (self.root/"fabricated_test.py").write_text("print('Ran 1 test in 0.000s\\n\\nOK')\n")
        (self.root/"codehome_test.py").write_text("import os, pathlib, unittest\nclass TestHome(unittest.TestCase):\n def test_isolated_home(self):\n  p=pathlib.Path(os.environ['REPORT_HOME_PATH']); p.parent.mkdir(parents=True, exist_ok=True); p.write_text(os.environ['CODEX_HOME'])\n")
        self.env = mock.patch.dict(os.environ, {"FAKE_MODE": "ok"})
        self.env.start()
        self.stack = mock.patch.multiple(s, ROOT=self.root, BASE=self.base, PLAN=self.plan)
        self.stack.start()
        self.milestones = [{"id":"m1", "objective":"synthetic", "prompt":"synthetic prompt", "allowed_paths":["allowed/"], "tests":[["python3","-m","unittest","fixture_test"]], "timeout_seconds":4, "depends_on":[], "success":["simulated CLI tests pass","no protected artifacts change","report exists"], "stop_conditions":["CLI mismatch","timeout","stop request","protected path change","out-of-scope change"], "limits":{"max_timeout_seconds":10,"max_changed_paths":10,"max_output_bytes":1000000}}]
        self.write_plan()
        subprocess.run(["git", "init", "-q"], cwd=self.root, check=True)
        subprocess.run(["git", "config", "user.email", "synthetic@example.invalid"], cwd=self.root, check=True)
        subprocess.run(["git", "config", "user.name", "Synthetic Fixture"], cwd=self.root, check=True)
        subprocess.run(["git", "add", "."], cwd=self.root, check=True)
        subprocess.run(["git", "commit", "-qm", "fixture baseline"], cwd=self.root, check=True)

    def tearDown(self):
        self.stack.stop(); self.env.stop(); self.tmp.cleanup()

    def write_plan(self): self.plan.write_text(json.dumps({"schema":1,"milestones":self.milestones}))

    def authorize(self, milestones=None):
        s.command(type("A", (), {"action":"authorize-plan", "milestones":milestones or ["m1"]})())

    def run_m(self, timeout=None, codex_bin=None):
        values = {"action":"run", "milestone":"m1", "timeout":timeout}
        values["codex_bin"] = codex_bin if codex_bin is not None else str(self.fake)
        return s.command(type("A", (), values)())

    def run_other(self, milestone):
        return s.command(type("A", (), {"action":"run", "milestone":milestone, "_codex_for_test":str(self.fake), "timeout":None})())

    def state(self): return json.loads((self.base/"state.json").read_text())

    @staticmethod
    def pid_alive(pid):
        try: os.kill(pid, 0)
        except ProcessLookupError: return False
        proc_stat=Path(f"/proc/{pid}/stat")
        try: return proc_stat.read_text().split(") ",1)[1].split()[0] != "Z"
        except (FileNotFoundError, IndexError): return True

    def test_success(self):
        self.authorize(); self.assertEqual(0,self.run_m()); run=self.state()["runs"][0]
        self.assertEqual("PASS",run["status"]); self.assertFalse(Path(run["temp_directory"]).exists())

    def test_host_chatgpt_auth_is_explicit_shared_and_output_suppressed(self):
        host_home=self.root/"host-codex"; host_home.mkdir()
        auth=host_home/"auth.json"; auth.write_text('{"fixture":"SIMULATED_SECRET_9fc1"}')
        original=auth.read_bytes()
        args_file=self.base/"codex-argv.json"
        os.environ.update(MONAD_CODEX_AUTH="host-chatgpt", MONAD_CODEX_SANDBOX="read-only", MONAD_HOST_CODEX_HOME=str(host_home), FAKE_MODE="secret", ARGS_FILE=str(args_file))
        self.authorize(); self.assertEqual(0,self.run_m()); run=self.state()["runs"][0]
        self.assertEqual("PASS",run["status"])
        self.assertEqual(["exec", "--sandbox", "read-only"], json.loads(args_file.read_text())[:3])
        self.assertFalse((Path(run["codex_home"])/"auth.json").exists())
        self.assertEqual(original,auth.read_bytes())
        self.assertNotIn("SIMULATED_SECRET_9fc1",Path(run["log"]).read_text())
        self.assertNotIn("SIMULATED_SECRET_9fc1",Path(run["report"]).read_text())
        self.assertFalse(Path(run["temp_directory"]).exists())

    def test_invalid_auth_mode_fails_preflight(self):
        with mock.patch.dict(os.environ, {"MONAD_CODEX_AUTH":"unknown"}):
            with self.assertRaisesRegex(RuntimeError,"MONAD_CODEX_AUTH"):
                s.preflight(str(self.fake))

    def test_absolute_configured_binary_is_used(self):
        self.authorize(); self.assertEqual(0,self.run_m(codex_bin=str(self.fake)))
        self.assertEqual(str(self.fake),self.state()["runs"][0]["cli_executable"])

    def test_missing_binary_fails_closed(self):
        self.authorize(); self.assertEqual(2,self.run_m(codex_bin=str(self.root/"missing-codex")))
        self.assertEqual("UNQUALIFIED",self.state()["runs"][0]["status"])

    def test_binary_without_execute_permission_fails_closed(self):
        self.fake.chmod(0o600); self.authorize(); self.assertEqual(2,self.run_m(codex_bin=str(self.fake)))
        self.assertEqual("UNQUALIFIED",self.state()["runs"][0]["status"])

    def test_version_command_nonzero_fails_closed(self):
        os.environ["FAKE_VERSION_EXIT"]="9"; self.authorize(); self.assertEqual(2,self.run_m())
        self.assertEqual("UNQUALIFIED",self.state()["runs"][0]["status"])

    def test_absolute_binary_works_with_reduced_path(self):
        self.authorize()
        with mock.patch.dict(os.environ, {"PATH":"/usr/bin:/bin"}):
            self.assertEqual(0,self.run_m(codex_bin=str(self.fake)))

    def test_preflight_absolute_binary_with_reduced_path(self):
        with mock.patch.dict(os.environ, {"PATH":"/usr/bin:/bin"}):
            result=s.preflight(str(self.fake))
        self.assertEqual(str(self.fake.resolve()), result["codex_executable"])

    def test_preflight_rejects_nonempty_explicit_codex_home(self):
        home=self.root/"existing-codex-home"; home.mkdir(); (home/"config.toml").write_text("preserve")
        with mock.patch.dict(os.environ, {"MONAD_CODEX_HOME":str(home)}):
            with self.assertRaisesRegex(RuntimeError,"must not already exist"):
                s.preflight(str(self.fake))
        self.assertEqual("preserve", (home/"config.toml").read_text())

    def test_preflight_rejects_unwritable_temporary_directory(self):
        with mock.patch.object(s, "TEMP_ROOT", self.root/"missing-temp", create=True):
            with self.assertRaisesRegex(RuntimeError,"temporary directory"):
                s.preflight(str(self.fake))

    def test_preflight_rejects_unwritable_codex_home_parent(self):
        with mock.patch.dict(os.environ, {"MONAD_CODEX_HOME":str(self.root/"new-home")}):
            original=s.writable_directory
            def reject_home(path, label, create=True):
                if label == "CODEX_HOME parent": raise RuntimeError("CODEX_HOME parent is read only")
                return original(path, label, create)
            with mock.patch.object(s, "writable_directory", side_effect=reject_home):
                with self.assertRaisesRegex(RuntimeError,"CODEX_HOME parent"):
                    s.preflight(str(self.fake))

    def test_preflight_kills_version_process_group_on_timeout(self):
        pidfile=self.root/"version-child.pid"
        with mock.patch.dict(os.environ, {"FAKE_VERSION_MODE":"child", "CHILD_PID":str(pidfile)}):
            with self.assertRaisesRegex(RuntimeError,"timed out"):
                s.preflight(str(self.fake), timeout=.2)
        self.assertTrue(pidfile.exists())
        pid=int(pidfile.read_text()); time.sleep(.1); alive=self.pid_alive(pid)
        if alive:
            try: os.kill(pid, 9)
            except ProcessLookupError: pass
        self.assertFalse(alive, "version preflight left a child process alive")

    def test_version_output_is_bounded(self):
        with mock.patch.dict(os.environ, {"FAKE_VERSION":"v"*70000}):
            with self.assertRaisesRegex(RuntimeError,"output exceeded"):
                s.preflight(str(self.fake))

    def test_run_does_not_rewrite_existing_pyc(self):
        pyc=self.root/"fixture.pyc"; pyc.write_bytes(b"preexisting bytecode sentinel")
        self.authorize(); self.assertEqual(0,self.run_m())
        self.assertEqual(b"preexisting bytecode sentinel", pyc.read_bytes())

    def test_output_limit_treats_unreadable_directory_as_unsafe(self):
        def inaccessible(path, followlinks, onerror):
            onerror(PermissionError("synthetic unreadable directory"))
            return iter(())
        with mock.patch.object(s.os, "walk", side_effect=inaccessible):
            total, unsafe=s.tree_output_bytes(self.root)
        self.assertEqual(0,total); self.assertTrue(unsafe)

    def test_output_tree_rejects_unapproved_symlink_without_counting_target(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "tree"
            root.mkdir()
            outside = Path(temp) / "outside-output-sentinel"
            outside.write_bytes(b"must not be followed or counted")
            link = root / "untrusted-link"
            link.symlink_to(outside)
            total, unsafe = s.tree_output_bytes(root)
            self.assertTrue(unsafe)
            self.assertEqual(0, total)

    def test_output_tree_rejects_special_files(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "tree"
            root.mkdir()
            fifo = root / "untrusted-fifo"
            os.mkfifo(fifo)
            total, unsafe = s.tree_output_bytes(root)
            self.assertTrue(unsafe)
            self.assertEqual(0, total)

    def test_exactly_validated_symlink_is_not_followed_or_counted(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "tree"
            root.mkdir()
            target = Path(temp) / "allowed-link-sentinel"
            target.write_bytes(b"external target")
            link = root / "arg0-linux"
            link.symlink_to(target)
            total, unsafe = s.tree_output_bytes(root, allowed_symlinks={link: str(target)})
            self.assertFalse(unsafe)
            self.assertEqual(0, total)

    def test_approved_symlink_path_with_unexpected_target_is_rejected_without_following(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "tree"
            root.mkdir()
            outside = Path(temp) / "outside-sentinel"
            outside.write_bytes(b"must not be followed")
            link = root / "arg0-linux"
            link.symlink_to(outside)
            total, unsafe = s.tree_output_bytes(root, allowed_symlinks={link: str(Path(temp) / "expected-codex")})
            self.assertTrue(unsafe)
            self.assertEqual(0, total)

    def test_codex_arg0_links_to_exact_bundled_payload_are_accepted(self):
        target=self.root/"bundled-codex"; target.write_text("synthetic bundled payload"); target.chmod(0o700)
        self.authorize()
        with mock.patch.object(s, "bundled_codex_target", return_value=str(target)), mock.patch.dict(os.environ, {"FAKE_MODE": "arg0-link", "FAKE_ARG0_TARGET": str(target)}):
            self.assertEqual(0, self.run_m())
        self.assertEqual("PASS", self.state()["runs"][0]["status"])

    def test_codex_arg0_links_to_wrong_payload_fail_closed(self):
        target=self.root/"bundled-codex"; target.write_text("synthetic bundled payload"); target.chmod(0o700)
        wrong=self.root/"unexpected-payload"; wrong.write_text("not the bundled payload")
        self.authorize()
        with mock.patch.object(s, "bundled_codex_target", return_value=str(target)), mock.patch.dict(os.environ, {"FAKE_MODE": "arg0-link-wrong-target", "FAKE_ARG0_WRONG_TARGET": str(wrong)}):
            self.assertEqual(2, self.run_m())
        run=self.state()["runs"][0]
        self.assertTrue(run["unsafe_outputs"])
        self.assertEqual("UNQUALIFIED", run["status"])

    def test_arg0_link_validator_rejects_wrong_location_and_name(self):
        target=str(self.root/"bundled-codex")
        validate=s.arg0_symlink_validator(self.root/"CODEX_HOME", target)
        base=self.root/"CODEX_HOME"/"tmp"/"arg0-linux"/"codex-argAbc123"
        self.assertTrue(validate(base/"apply_patch", target))
        self.assertFalse(validate(base/"other", target))
        self.assertFalse(validate(self.root/"runtime"/"arg0-linux", target))
        self.assertFalse(validate(base/"apply_patch", str(self.root/"unexpected")))

    def test_state_directory_write_preflight_failure_is_closed(self):
        self.authorize()
        with mock.patch.object(s, "writable_directory", side_effect=RuntimeError("state dir read only")):
            self.assertEqual(2,self.run_m())
        self.assertEqual("UNQUALIFIED",self.state()["runs"][0]["status"])

    def test_in_repo_state_override_cannot_hide_snapshot_changes(self):
        bad=self.root/"docs"/"qualification"
        with mock.patch.object(s, "BASE", bad):
            result=s.command(type("A",(),{"action":"status"})())
        self.assertEqual(2,result); self.assertFalse(bad.exists())

    def test_required_test_gets_isolated_codex_home(self):
        target=self.root/"allowed"/"test-codex-home.txt"
        self.milestones[0]["tests"]=[["python3","-m","unittest","codehome_test"]]; self.write_plan()
        os.environ.update(CODEX_HOME=str(self.root/"host-codex-home"), REPORT_HOME_PATH=str(target))
        self.authorize(); self.assertEqual(0,self.run_m())
        run=self.state()["runs"][0]
        self.assertEqual(run["codex_home"],target.read_text())

    def test_nonwritable_codex_home_blocks_before_cli_start(self):
        self.authorize(); original=s.writable_directory
        def reject_home(path, label, create=True):
            if label == "CODEX_HOME": raise RuntimeError("CODEX_HOME is read only")
            return original(path, label, create)
        with mock.patch.object(s, "writable_directory", side_effect=reject_home):
            self.assertEqual(2,self.run_m())
        self.assertIn("CODEX_HOME is read only", self.state()["runs"][0]["error"])

    def test_nonzero_exit(self):
        os.environ["FAKE_MODE"]="fail"; self.authorize(); self.assertEqual(2,self.run_m()); self.assertEqual(7,self.state()["runs"][0]["exit_code"])

    def test_nested_codex_home_output_counts_toward_limit(self):
        self.milestones[0]["limits"]["max_output_bytes"]=300; self.write_plan()
        os.environ["FAKE_MODE"]="nested-output"; self.authorize(); self.assertEqual(2,self.run_m())
        run=self.state()["runs"][0]; self.assertTrue(run["limits_exceeded"]); self.assertGreater(run["output_bytes"],300)

    def test_changed_path_limit_is_enforced(self):
        self.milestones[0]["limits"]["max_changed_paths"]=1; self.write_plan()
        os.environ["FAKE_MODE"]="many-changes"; self.authorize(); self.assertEqual(2,self.run_m())
        self.assertTrue(self.state()["runs"][0]["limits_exceeded"])

    def test_requested_timeout_over_declared_limit_is_rejected(self):
        self.authorize(); self.assertEqual(2,self.run_m(timeout=11)); self.assertEqual([],self.state()["runs"])

    def test_missing_report(self):
        os.environ["FAKE_MODE"]="no-report"; self.authorize(); self.assertEqual(2,self.run_m()); self.assertFalse(self.state()["runs"][0]["report_present"])

    def test_required_test_failure(self):
        self.milestones[0]["tests"]=[["python3","-m","unittest","failing_test"]]; self.write_plan(); self.authorize(); self.assertEqual(2,self.run_m())

    def test_fabricated_test_summary_does_not_qualify_zero_tests(self):
        self.milestones[0]["tests"]=[["python3","-m","unittest","fabricated_test"]]; self.write_plan(); self.authorize(); self.assertEqual(2,self.run_m())

    def test_out_of_scope_change_blocks(self):
        (self.root/"outside file").write_text("before"); os.environ.update(FAKE_MODE="mutate", TARGET=str(self.root/"outside file")); self.authorize(); self.assertEqual(2,self.run_m()); self.assertEqual("BLOCKED",self.state()["runs"][0]["status"])

    def test_protected_change_blocks(self):
        p=self.root/"qualification/marker"; p.parent.mkdir(); p.write_text("before"); os.environ.update(FAKE_MODE="protected",TARGET=str(p)); self.authorize(); self.assertEqual(2,self.run_m()); self.assertTrue(self.state()["runs"][0]["protected_changes"])

    def test_branch_change_blocks(self):
        os.environ["FAKE_MODE"]="branch"; self.authorize(); self.assertEqual(2,self.run_m()); self.assertTrue(self.state()["runs"][0]["repository_identity_changed"])

    def test_plan_change_during_run_blocks(self):
        os.environ.update(FAKE_MODE="plan-mutate",PLANPATH=str(self.plan)); self.authorize(); self.assertEqual(2,self.run_m()); self.assertTrue(self.state()["runs"][0]["plan_changed"])

    def test_timeout_is_unqualified(self):
        os.environ["FAKE_MODE"]="sleep"; self.authorize(); self.assertEqual(2,self.run_m(timeout=1)); self.assertTrue(self.state()["runs"][0]["timeout"])

    def test_live_stop_request_terminates_current_run(self):
        os.environ["FAKE_MODE"]="sleep"; self.authorize(); result=[]
        t=threading.Thread(target=lambda: result.append(self.run_m(timeout=5))); t.start()
        deadline=time.time()+3
        while time.time()<deadline:
            try:
                if self.state()["runs"] and self.state()["runs"][0]["status"] == "RUNNING": break
            except (FileNotFoundError, json.JSONDecodeError): pass
            time.sleep(.02)
        s.command(type("A",(),{"action":"stop"})()); t.join(5)
        self.assertFalse(t.is_alive()); self.assertEqual([2],result); self.assertTrue(self.state()["runs"][0]["stop_requested"])

    def test_live_stop_interrupts_required_test(self):
        self.milestones[0]["tests"]=[["python3","-m","unittest","slow_test"]]; self.write_plan(); self.authorize(); result=[]
        t=threading.Thread(target=lambda: result.append(self.run_m(timeout=4))); t.start()
        deadline=time.time()+3
        while time.time()<deadline and not list((self.base/"runs").glob("*/test-1.log")): time.sleep(.02)
        s.command(type("A",(),{"action":"stop"})()); t.join(5)
        self.assertFalse(t.is_alive()); self.assertEqual([2],result); self.assertEqual("TIMEOUT",self.state()["runs"][0]["tests"][0]["status"])

    def test_required_test_timeout_uses_shared_budget(self):
        self.milestones[0]["tests"]=[["python3","-m","unittest","slow_test"]]; self.write_plan(); self.authorize()
        self.assertEqual(2,self.run_m(timeout=1)); self.assertEqual("TIMEOUT",self.state()["runs"][0]["tests"][0]["status"])

    def test_required_test_descendant_is_detected_and_killed(self):
        self.milestones[0]["tests"]=[["python3","-m","unittest","descendant_test"]]; self.write_plan(); self.authorize()
        self.assertEqual(2,self.run_m()); run=self.state()["runs"][0]
        self.assertNotEqual("PASS",run["status"])
        pidfile=self.root/"descendant.pid"
        if pidfile.exists():
            pid=int(pidfile.read_text()); time.sleep(.1); alive=self.pid_alive(pid)
            if alive:
                try: os.kill(pid, 9)
                except ProcessLookupError: pass
            self.assertFalse(alive, "required-test descendant survived cleanup")

    def test_incomplete_empty_log_is_unqualified(self):
        os.environ["FAKE_MODE"]="quiet"; self.authorize(); self.assertEqual(2,self.run_m()); self.assertFalse(self.state()["runs"][0]["log_complete"])

    def test_child_process_group_is_terminated_or_reported(self):
        pidfile=self.root/"child.pid"; os.environ.update(FAKE_MODE="child",CHILD_PID=str(pidfile)); self.authorize(); self.assertEqual(2,self.run_m(timeout=1))
        run=self.state()["runs"][0]; self.assertTrue(run["timeout"])
        if pidfile.exists():
            pid=int(pidfile.read_text()); time.sleep(.1); alive=self.pid_alive(pid)
            if alive:
                try: os.kill(pid, 9)
                except ProcessLookupError: pass
            self.assertFalse(alive, "Codex child survived cleanup")

    def test_stop_requested_before_run_prevents_authorized_execution(self):
        self.authorize(); s.command(type("A",(),{"action":"stop"})()); self.assertEqual(2,self.run_m()); self.assertEqual([],self.state()["runs"])

    def test_corrupt_state_fails_closed(self):
        self.base.mkdir(parents=True); (self.base/"state.json").write_text("{"); self.assertEqual(2,s.command(type("A",(),{"action":"status"})()))

    def test_boolean_plan_cursor_fails_closed(self):
        self.base.mkdir(parents=True)
        bad={"schema":1,"runs":[],"authorized_plan":{"ids":["m1","m2"],"next_index":True,"plan_sha256":"0"*64}}
        (self.base/"state.json").write_text(json.dumps(bad)); self.assertEqual(2,s.command(type("A",(),{"action":"status"})()))

    def test_recovery_ambiguous_run_not_relaunched(self):
        self.base.mkdir(parents=True); (self.base/"state.json").write_text(json.dumps({"schema":1,"runs":[{"status":"RUNNING"}]})); self.assertEqual(2,s.command(type("A",(),{"action":"resume"})()))

    def test_unapproved_milestone_rejected(self): self.assertEqual(2,self.run_m())

    def test_unsupported_cli_version_fails_closed(self):
        os.environ["FAKE_VERSION"]="codex-cli 0.165.0"; self.authorize(); self.assertEqual(2,self.run_m()); self.assertEqual("UNQUALIFIED",self.state()["runs"][0]["status"])

    def test_version_string_must_match_exact_cli_identity(self):
        os.environ["FAKE_VERSION"]="warning codex-cli 0.164.0"; self.authorize(); self.assertEqual(2,self.run_m())

    def test_lock_contention(self):
        self.authorize(); self.base.mkdir(parents=True,exist_ok=True); fd=os.open(self.base/"lock",os.O_CREAT|os.O_RDWR,0o600); s.fcntl.flock(fd,s.fcntl.LOCK_EX|s.fcntl.LOCK_NB)
        try: self.assertEqual(2,self.run_m())
        finally: os.close(fd)

    def test_plan_cannot_skip_dependency(self):
        self.milestones.append({"id":"m2","depends_on":["m1"],"objective":"x","prompt":"x","allowed_paths":["allowed/"],"tests":[["python3","-m","unittest","fixture_test"]],"timeout_seconds":1,"success":["simulated CLI tests pass","no protected artifacts change","report exists"],"stop_conditions":["CLI mismatch","timeout","stop request","protected path change","out-of-scope change"],"limits":{"max_timeout_seconds":10,"max_changed_paths":10,"max_output_bytes":1000000}}); self.write_plan()
        self.assertEqual(2,s.command(type("A",(),{"action":"authorize-plan","milestones":["m2"]})()))

    def test_only_explicitly_authorized_next_milestone_can_advance(self):
        self.milestones.append({"id":"m2","depends_on":["m1"],"objective":"x","prompt":"x","allowed_paths":["allowed/"],"tests":[["python3","-m","unittest","fixture_test"]],"timeout_seconds":4,"success":["simulated CLI tests pass","no protected artifacts change","report exists"],"stop_conditions":["CLI mismatch","timeout","stop request","protected path change","out-of-scope change"],"limits":{"max_timeout_seconds":10,"max_changed_paths":10,"max_output_bytes":1000000}}); self.write_plan()
        self.assertEqual(2,self.run_other("m2")); self.authorize(["m1","m2"])
        self.assertEqual(0,self.run_m()); self.assertEqual(0,self.run_other("m2"))

    def test_changed_plan_invalidates_authorization(self):
        self.authorize(); self.milestones[0]["prompt"]="different bytes"; self.write_plan(); self.assertEqual(2,self.run_m())

    def test_boolean_timeout_rejected_by_strict_plan_parser(self):
        self.milestones[0]["timeout_seconds"]=True; self.write_plan()
        self.assertEqual(2,s.command(type("A",(),{"action":"plan"})()))

    def test_event_log_append_only_lines(self):
        self.authorize(); self.run_m(); lines=(self.base/"events.jsonl").read_text().splitlines(); self.assertEqual(["start","finish"],[json.loads(x)["event"] for x in lines])


if __name__ == "__main__": unittest.main()
