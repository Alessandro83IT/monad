#!/usr/bin/env python3
"""Conservative local Codex milestone supervisor (synthetic qualification only)."""
from __future__ import annotations

import argparse
import datetime as dt
import fcntl
import hashlib
import json
import os
import re
import shutil
from pathlib import Path
import signal
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import uuid
from collections.abc import Callable

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(os.environ.get("MONAD_SUPERVISOR_STATE_DIR", ROOT / ".codex-supervisor"))
PLAN = Path(os.environ.get("MONAD_CODEX_PLAN_FILE", ROOT / "tools" / "codex-supervisor" / "milestones.json"))
CODEX_EXECUTABLE = os.environ.get("MONAD_CODEX_BIN", "codex")
TEMP_ROOT = Path(os.environ.get("MONAD_SUPERVISOR_TMPDIR", tempfile.gettempdir()))
PROTECTED = ("qualification/", "docs/qualification/", "scripts/", ".codex/runs/", ".codex/autonomous-mandate.md")


def utc() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat()


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for b in iter(lambda: f.read(1024 * 1024), b""):
            h.update(b)
    return h.hexdigest()


def atomic_json(path: Path, obj: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp-" + uuid.uuid4().hex)
    data = (json.dumps(obj, sort_keys=True, indent=2) + "\n").encode()
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data); f.flush(); os.fsync(f.fileno())
        os.replace(tmp, path)
        dfd = os.open(path.parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try: os.fsync(dfd)
        finally: os.close(dfd)
    finally:
        try: tmp.unlink()
        except FileNotFoundError: pass


def git(*args: str) -> str:
    p = subprocess.run(["git", *args], cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode: raise RuntimeError("git " + " ".join(args) + " failed: " + p.stderr.strip())
    return p.stdout


def snapshot() -> dict[str, str]:
    status = subprocess.run(["git", "status", "--porcelain=v1", "-z", "--untracked-files=all"], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if status.returncode: raise RuntimeError("git status failed: " + status.stderr.decode(errors="replace").strip())
    rows = status.stdout.split(b"\0")
    out = {}
    i = 0
    while i < len(rows) and rows[i]:
        row = os.fsdecode(rows[i]); i += 1
        if len(row) < 4: raise RuntimeError("malformed git status record")
        code, name = row[:2], row[3:]
        if "R" in code or "C" in code:
            raise RuntimeError("git rename/copy status is ambiguous")
        if code == "??":
            # A path beginning with a quote is literal in -z output.
            pass
        elif not code.strip(): raise RuntimeError("empty git status code")
        if name == ".codex-supervisor" or name.startswith(".codex-supervisor/"):
            continue
        p = ROOT / name
        if p.is_file() and not p.is_symlink(): out[name] = digest(p)
        else: out[name] = "<non-regular-or-missing>"
    out["@@HEAD"] = git("rev-parse", "HEAD").strip()
    out["@@branch"] = git("branch", "--show-current").strip()
    return out


def load_plan() -> list[dict]:
    obj = json.loads(PLAN.read_text())
    if obj.get("schema") != 1 or not isinstance(obj.get("milestones"), list): raise ValueError("invalid plan schema")
    milestones = obj["milestones"]
    ids = [m.get("id") for m in milestones]
    if len(ids) != len(set(ids)) or not all(isinstance(i, str) and re.fullmatch(r"[a-z0-9][a-z0-9-]{0,63}", i) for i in ids): raise ValueError("invalid or duplicate milestone id")
    required = {"id", "objective", "prompt", "allowed_paths", "tests", "timeout_seconds", "depends_on", "success", "stop_conditions", "limits"}
    allowed_success = {"simulated CLI tests pass", "no protected artifacts change", "report exists"}
    allowed_stops = {"CLI mismatch", "timeout", "stop request", "protected path change", "out-of-scope change"}
    known = set(ids)
    for m in milestones:
        if set(m) != required: raise ValueError("milestone fields do not match strict schema")
        if any(not isinstance(m[k], str) or not m[k] for k in ("objective", "prompt")): raise ValueError("objective and prompt must be nonempty text")
        if isinstance(m["timeout_seconds"], bool) or not isinstance(m["timeout_seconds"], int) or m["timeout_seconds"] <= 0: raise ValueError("invalid milestone timeout")
        if not isinstance(m["tests"], list) or not m["tests"] or any(not isinstance(c, list) or not c or any(not isinstance(a, str) for a in c) for c in m["tests"]): raise ValueError("invalid/empty required test list")
        if any(c[:3] != ["python3", "-m", "unittest"] for c in m["tests"]): raise ValueError("only Python unittest argv tests are supported")
        if not isinstance(m["allowed_paths"], list) or not m["allowed_paths"] or any(not isinstance(x, str) or not x or x.startswith("/") or ".." in Path(x).parts or "." in Path(x).parts for x in m["allowed_paths"]): raise ValueError("invalid allowed path")
        if not isinstance(m["depends_on"], list) or any(not isinstance(d, str) or d not in known for d in m["depends_on"]): raise ValueError("unknown milestone dependency")
        limits = m["limits"]
        if not isinstance(limits, dict) or set(limits) != {"max_timeout_seconds", "max_changed_paths", "max_output_bytes"}: raise ValueError("invalid explicit milestone limits")
        if any(isinstance(limits[k], bool) or not isinstance(limits[k], int) or limits[k] <= 0 for k in limits): raise ValueError("milestone limits must be positive integers")
        if m["timeout_seconds"] > limits["max_timeout_seconds"]: raise ValueError("milestone timeout exceeds declared limit")
        if not isinstance(m["success"], list) or set(m["success"]) != allowed_success or not isinstance(m["stop_conditions"], list) or set(m["stop_conditions"]) != allowed_stops: raise ValueError("unsupported/missing success or stop criterion")
    return milestones


def protected_changes(before: dict, after: dict) -> list[str]:
    changed = {p for p in before.keys() | after.keys() if before.get(p) != after.get(p)}
    return sorted(p for p in changed if p != "docs/qualification/alpha-m1-session-64-candidate.md" and any(p.startswith(prefix) for prefix in PROTECTED))


def acquire_lock() -> int | None:
    lock_path = BASE / "lock"
    fd = os.open(lock_path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    try: fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError: os.close(fd); return None
    return fd


def read_state(path: Path) -> dict:
    if not path.exists(): return {"schema": 1, "runs": [], "authorized_plan": None}
    state = json.loads(path.read_text())
    if not isinstance(state, dict) or state.get("schema") != 1 or not isinstance(state.get("runs"), list): raise ValueError("invalid state schema")
    if any(not isinstance(r, dict) or not isinstance(r.get("status"), str) for r in state["runs"]): raise ValueError("invalid run record")
    auth = state.get("authorized_plan")
    if auth is not None:
        if not isinstance(auth, dict) or not isinstance(auth.get("ids"), list) or isinstance(auth.get("next_index"), bool) or not isinstance(auth.get("next_index"), int) or not isinstance(auth.get("plan_sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", auth["plan_sha256"]): raise ValueError("invalid plan authorization state")
        if not 0 <= auth["next_index"] <= len(auth["ids"]): raise ValueError("invalid plan cursor")
    return state


def terminate_group(proc: subprocess.Popen, grace: float = 5.0) -> bool:
    """Bounded group cleanup; return true when membership cannot be disproved."""
    try: os.killpg(proc.pid, signal.SIGTERM)
    except ProcessLookupError: pass
    try: proc.wait(timeout=grace)
    except subprocess.TimeoutExpired:
        try: os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError: pass
        try: proc.wait(timeout=grace)
        except subprocess.TimeoutExpired: return True
    try: os.killpg(proc.pid, 0)
    except ProcessLookupError: return False
    try: os.killpg(proc.pid, signal.SIGKILL)
    except ProcessLookupError: return False
    time.sleep(.05)
    try: os.killpg(proc.pid, 0); return True
    except ProcessLookupError: return False


def append_event(path: Path, obj: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_APPEND | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, "ab") as f:
        f.write((json.dumps(obj, sort_keys=True) + "\n").encode()); f.flush(); os.fsync(f.fileno())


def writable_directory(path: Path, label: str, create: bool = True) -> None:
    """Check a directory by creating a unique probe; never replace existing paths."""
    try:
        if create:
            path.mkdir(parents=True, exist_ok=True, mode=0o700)
        if not path.is_dir(): raise OSError("not a directory")
        probe = path / (".monad-supervisor-probe-" + uuid.uuid4().hex)
        fd = os.open(probe, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
        os.close(fd)
        probe.unlink()
    except OSError as e:
        raise RuntimeError(f"{label} is not safely writable: {path}: {e}") from e


def validate_state_directory() -> None:
    """Permit state inside the repository only at its fixed excluded location."""
    try: relative = BASE.resolve().relative_to(ROOT.resolve())
    except ValueError: return
    if relative != Path(".codex-supervisor"):
        raise RuntimeError("state directory inside the repository must be exactly .codex-supervisor")


def resolve_executable(value: str) -> str:
    found = value if os.path.isabs(value) else __import__("shutil").which(value)
    if not found: raise RuntimeError(f"Codex executable not found: {value}")
    resolved = str(Path(found).resolve(strict=True))
    if not os.path.isfile(resolved) or not os.access(resolved, os.X_OK): raise RuntimeError(f"Codex executable is not executable: {resolved}")
    return resolved


def preflight(codex_bin: str, timeout: int = 10) -> dict:
    executable = resolve_executable(codex_bin)
    writable_directory(BASE, "supervisor state/log directory")
    writable_directory(TEMP_ROOT, "temporary directory", create=False)
    version = run_version(executable, timeout)
    if version.returncode or version.stdout.strip() != "codex-cli 0.164.0":
        raise RuntimeError(f"Codex CLI version unavailable or mismatched: {version.stdout.strip()}")
    plan = load_plan()
    required_commands = {"git", *(test[0] for milestone in plan for test in milestone["tests"])}
    missing = sorted(c for c in required_commands if not shutil.which(c))
    if missing: raise RuntimeError("required host commands unavailable: " + ", ".join(missing))
    git("rev-parse", "--show-toplevel")
    # Use a fresh CODEX_HOME by default. Explicit homes must be empty, preventing
    # an invocation from silently rewriting a user's existing Codex state.
    configured = os.environ.get("MONAD_CODEX_HOME")
    if configured:
        home = Path(configured).expanduser().resolve()
        if home.exists(): raise RuntimeError(f"configured CODEX_HOME must not already exist: {home}")
        writable_directory(home.parent, "CODEX_HOME parent")
    auth_mode = os.environ.get("MONAD_CODEX_AUTH", "isolated")
    if auth_mode not in {"isolated", "host-chatgpt"}:
        raise RuntimeError("MONAD_CODEX_AUTH must be isolated or host-chatgpt")
    if auth_mode == "host-chatgpt":
        host_home = Path(os.environ.get("MONAD_HOST_CODEX_HOME", str(Path.home() / ".codex"))).expanduser().resolve()
        auth_file = host_home / "auth.json"
        if auth_file.is_symlink() or not auth_file.is_file():
            raise RuntimeError("host ChatGPT auth file is missing or not a regular file")
    return {"codex_executable": executable, "codex_version": version.stdout.strip(), "state_dir": str(BASE), "temp_dir": str(TEMP_ROOT), "plan_sha256": digest(PLAN), "milestone_count": len(plan), "codex_home_policy": "fresh per run" if not configured else str(home), "auth_mode": auth_mode}


def run_version(executable: str, timeout: float, max_output_bytes: int = 65536) -> subprocess.CompletedProcess:
    """Run version query with isolated paths, bounded output, and group cleanup."""
    writable_directory(TEMP_ROOT, "temporary directory", create=False)
    scratch = Path(tempfile.mkdtemp(prefix="monad-codex-version-", dir=TEMP_ROOT))
    home = scratch / "home"; home.mkdir(mode=0o700)
    tmp = scratch / "tmp"; tmp.mkdir(mode=0o700)
    output_path = scratch / "version.out"
    env = os.environ.copy()
    env.update({"CODEX_HOME":str(home), "HOME":str(home), "XDG_CONFIG_HOME":str(home/"config"), "XDG_CACHE_HOME":str(home/"cache"), "XDG_DATA_HOME":str(home/"data"), "XDG_STATE_HOME":str(home/"state"), "XDG_RUNTIME_DIR":str(tmp), "TMPDIR":str(tmp), "TMP":str(tmp), "TEMP":str(tmp), "PYTHONDONTWRITEBYTECODE":"1"})
    proc = None
    residual = False
    group_checked = False
    try:
        with output_path.open("xb") as out:
            proc = subprocess.Popen([executable, "--version"], cwd=ROOT, stdout=out, stderr=subprocess.STDOUT, text=False, start_new_session=True, env=env)
            deadline = time.monotonic() + timeout
            while proc.poll() is None:
                if output_path.stat().st_size > max_output_bytes:
                    residual = terminate_group(proc, grace=1.0)
                    group_checked = True
                    raise RuntimeError("Codex --version output exceeded preflight limit")
                if time.monotonic() >= deadline:
                    residual = terminate_group(proc, grace=1.0)
                    group_checked = True
                    if residual: raise ProcessGroupFailure("Codex --version timed out; residual process group remains", proc.pid)
                    raise RuntimeError("Codex --version timed out")
                time.sleep(.05)
            residual = terminate_group(proc, grace=0.25)
            group_checked = True
            if residual: raise ProcessGroupFailure("Codex --version left a residual process group", proc.pid)
        size = output_path.stat().st_size
        if size > max_output_bytes: raise RuntimeError("Codex --version output exceeded preflight limit")
        output = output_path.read_text(errors="replace")
        return subprocess.CompletedProcess([executable, "--version"], proc.returncode, output, None)
    except Exception:
        if proc is not None and not group_checked:
            residual = terminate_group(proc, grace=1.0)
        if residual and proc is not None:
            raise ProcessGroupFailure("Codex --version left a residual process group", proc.pid)
        raise
    finally:
        if not residual:
            shutil.rmtree(scratch, ignore_errors=True)


class ProcessGroupFailure(RuntimeError):
    def __init__(self, message: str, pid: int):
        super().__init__(message)
        self.pid = pid


def tree_output_bytes(*roots: Path, allowed_symlinks: dict[Path, str] | None = None,
                      allowed_symlink_validator: Callable[[Path, str], bool] | None = None) -> tuple[int, bool]:
    """Count regular output without following links; narrowly accept exact link facts."""
    total = 0
    unsafe = False
    seen = set()
    allowed_symlinks = allowed_symlinks or {}
    def walk_error(_error: OSError) -> None:
        nonlocal unsafe
        unsafe = True
    for root in roots:
        try: root_mode = root.lstat().st_mode
        except FileNotFoundError: continue
        if not stat.S_ISDIR(root_mode):
            unsafe = True
            continue
        for current, dirs, files in os.walk(root, followlinks=False, onerror=walk_error):
            for name in list(dirs):
                item = Path(current) / name
                mode = item.lstat().st_mode
                if not stat.S_ISDIR(mode):
                    dirs.remove(name); unsafe = True
            for name in files:
                item = Path(current) / name
                mode = item.lstat().st_mode
                if stat.S_ISREG(mode):
                    key = item.resolve()
                    if key not in seen:
                        total += item.stat(follow_symlinks=False).st_size; seen.add(key)
                elif stat.S_ISLNK(mode) and (item in allowed_symlinks or allowed_symlink_validator is not None):
                    # Compare the literal link payload. Never resolve/stat its target.
                    try:
                        target = os.readlink(item)
                        exact_match = item in allowed_symlinks and target == allowed_symlinks[item]
                        validated = allowed_symlink_validator is not None and allowed_symlink_validator(item, target)
                        if not (exact_match or validated): unsafe = True
                    except OSError:
                        unsafe = True
                else: unsafe = True
    return total, unsafe


ARG0_LINK_NAMES = frozenset({"codex", "applypatch", "apply_patch", "codex-linux-sandbox"})


def bundled_codex_target(codex_bin: str) -> str | None:
    """Find the optional native Codex payload beside its JavaScript launcher."""
    if sys.platform != "linux": return None
    machine = __import__("platform").machine().lower()
    targets = {"x86_64": ("x64", "x86_64-unknown-linux-musl"), "aarch64": ("arm64", "aarch64-unknown-linux-musl")}
    if machine not in targets: return None
    package_arch, triple = targets[machine]
    package_root = Path(codex_bin).parent.parent
    candidates = (
        package_root / "node_modules" / "@openai" / f"codex-linux-{package_arch}" / "vendor" / triple / "bin" / "codex",
        package_root / "vendor" / triple / "bin" / "codex",
    )
    for candidate in candidates:
        try: mode = candidate.lstat().st_mode
        except OSError: continue
        if stat.S_ISREG(mode) and os.access(candidate, os.X_OK): return str(candidate)
    return None


def arg0_symlink_validator(codex_home: Path, target: str | None) -> Callable[[Path, str], bool]:
    """Allow only Codex's known transient arg0 links to its exact bundled payload."""
    root = codex_home / "tmp" / "arg0-linux"
    def validate(item: Path, literal_target: str) -> bool:
        try: relative = item.relative_to(root)
        except ValueError: return False
        if len(relative.parts) != 2: return False
        temp_name, link_name = relative.parts
        if not re.fullmatch(r"codex-arg[A-Za-z0-9_-]{1,32}", temp_name): return False
        return link_name in ARG0_LINK_NAMES and target is not None and literal_target == target
    return validate


def command(args: argparse.Namespace) -> int:
    try: validate_state_directory()
    except Exception as e:
        print(f"BLOCKED: invalid state directory: {e}"); return 2
    if args.action in {"authorize-plan", "run", "stop"}: BASE.mkdir(parents=True, exist_ok=True)
    state_path = BASE / "state.json"
    try: state = read_state(state_path)
    except Exception as e:
        print(f"BLOCKED: corrupt state: {e}"); return 2
    if args.action == "plan":
        try: plan = load_plan()
        except Exception as e: print(f"BLOCKED: invalid plan: {e}"); return 2
        for m in plan: print(f"{m['id']} [{', '.join(m['depends_on']) or 'none'}] {m['objective']} ({m['timeout_seconds']}s)")
        print("Execution requires an explicitly authorized plan via authorize-plan; no automatic authorization is inferred.")
        return 0
    if args.action == "preflight":
        try:
            result = preflight(getattr(args, "codex_bin", None) or CODEX_EXECUTABLE)
            result["plan_sha256"] = digest(PLAN)
            print(json.dumps(result, sort_keys=True, indent=2)); return 0
        except Exception as e:
            print(f"BLOCKED: preflight failed: {e}"); return 2
    if args.action == "authorize-plan":
        lock_fd = acquire_lock()
        if lock_fd is None: print("BLOCKED: supervisor lock held"); return 2
        try:
            try: state = read_state(state_path)
            except Exception as e: print(f"BLOCKED: corrupt state: {e}"); return 2
            try: plan = load_plan()
            except Exception as e: print(f"BLOCKED: invalid plan: {e}"); return 2
            ids = args.milestones
            valid = {m["id"] for m in plan}
            if not ids or any(i not in valid for i in ids): print("BLOCKED: unknown/empty plan"); return 2
            for i, nxt in zip(ids, ids[1:]):
                if i not in next(m for m in plan if m["id"] == nxt)["depends_on"]: print("BLOCKED: sequence violates declared dependency"); return 2
            positions = {value: n for n, value in enumerate(ids)}
            for value in ids:
                for dep in next(m for m in plan if m["id"] == value)["depends_on"]:
                    if dep not in positions or positions[dep] >= positions[value]: print("BLOCKED: plan omits or misorders a dependency"); return 2
            state["authorized_plan"] = {"ids": ids, "next_index": 0, "plan_sha256": digest(PLAN), "authorized_at": utc(), "by_uid": os.getuid()}
            atomic_json(state_path, state)
            try: (BASE / "stop.request").unlink()
            except FileNotFoundError: pass
            print("Plan authorization recorded."); return 0
        finally:
            fcntl.flock(lock_fd, fcntl.LOCK_UN); os.close(lock_fd)
    if args.action == "status":
        state["pending_stop"] = (BASE / "stop.request").exists()
        print(json.dumps(state, indent=2, sort_keys=True)); return 0
    if args.action == "report":
        for run in state.get("runs", []):
            print(json.dumps(run, sort_keys=True))
        return 0
    if args.action == "stop":
        atomic_json(BASE / "stop.request", {"requested_at": utc(), "by_uid": os.getuid()})
        print("Controlled stop requested; active process will be signaled by its supervisor."); return 0
    if args.action == "resume":
        active = [r for r in state.get("runs", []) if r.get("status") == "RUNNING" or r.get("residual_process_group")]
        if active: print("BLOCKED: incomplete run is ambiguous; inspect its log and process state; no relaunch performed."); return 2
        print("No resumable interrupted run. Completed runs are not replayed."); return 0
    if args.action != "run": return 2
    if args.timeout is not None and args.timeout <= 0: print("BLOCKED: timeout must be positive"); return 2
    if (BASE / "stop.request").exists():
        print("BLOCKED: stop request is active; explicitly authorize a new plan to clear it"); return 2
    auth = state.get("authorized_plan")
    idx = auth.get("next_index", 0) if auth else 0
    if not auth or auth.get("plan_sha256") != digest(PLAN):
        print("BLOCKED: authorized plan bytes changed; authorize the reviewed plan again"); return 2
    if not auth.get("ids") or idx >= len(auth["ids"]) or auth["ids"][idx] != args.milestone:
        print("BLOCKED: milestone not first in an explicitly authorized plan"); return 2
    try: m = next((m for m in load_plan() if m["id"] == args.milestone), None)
    except Exception as e: print(f"BLOCKED: invalid plan: {e}"); return 2
    if not m: print("BLOCKED: unknown milestone"); return 2
    if args.timeout is not None and args.timeout > m["limits"]["max_timeout_seconds"]:
        print("BLOCKED: requested timeout exceeds declared milestone limit"); return 2
    lock_fd = acquire_lock()
    if lock_fd is None: print("BLOCKED: supervisor lock held"); return 2
    # Re-read under lock so authorization cannot race a concurrent update.
    try: state = read_state(state_path)
    except Exception as e:
        fcntl.flock(lock_fd, fcntl.LOCK_UN); os.close(lock_fd); print(f"BLOCKED: corrupt state: {e}"); return 2
    if any(r.get("status") == "RUNNING" for r in state.get("runs", [])):
        fcntl.flock(lock_fd, fcntl.LOCK_UN); os.close(lock_fd); print("BLOCKED: incomplete run requires manual recovery review"); return 2
    auth = state.get("authorized_plan"); idx = auth.get("next_index", 0) if auth else 0
    if (BASE / "stop.request").exists() or not auth or auth.get("plan_sha256") != digest(PLAN) or idx >= len(auth.get("ids", [])) or auth["ids"][idx] != args.milestone:
        fcntl.flock(lock_fd, fcntl.LOCK_UN); os.close(lock_fd); print("BLOCKED: run authorization changed or stop is active"); return 2
    rid = str(uuid.uuid4()); run_dir = BASE / "runs" / rid; run_dir.mkdir(parents=True, mode=0o700)
    log = run_dir / "codex.log"; report = run_dir / "report.txt"
    try: before = snapshot()
    except Exception as e:
        fcntl.flock(lock_fd, fcntl.LOCK_UN); os.close(lock_fd); print(f"BLOCKED: initial Git snapshot failed: {e}"); return 2
    record = {"id": rid, "milestone": m["id"], "status": "RUNNING", "started_at": utc(), "before": before, "log": str(log), "report": str(report), "process_launch_state": "NOT_STARTED"}
    state["runs"].append(record); atomic_json(state_path, state)
    append_event(BASE / "events.jsonl", {"event": "start", **{k: record[k] for k in ("id", "milestone", "started_at")}})
    proc = None
    scratch = None
    residual = False
    output_limit = False
    try:
        deadline = time.monotonic() + min(args.timeout or m["timeout_seconds"], m["timeout_seconds"])
        codex_bin = getattr(args, "_codex_for_test", None) or getattr(args, "codex_bin", None) or CODEX_EXECUTABLE
        codex_bin = resolve_executable(codex_bin)
        writable_directory(BASE, "supervisor state/log directory")
        writable_directory(TEMP_ROOT, "temporary directory", create=False)
        codex_home = run_dir / "CODEX_HOME"
        if os.environ.get("MONAD_CODEX_HOME"):
            codex_home = Path(os.environ["MONAD_CODEX_HOME"]).expanduser().resolve()
            if codex_home.exists(): raise RuntimeError(f"configured CODEX_HOME must not already exist: {codex_home}")
            writable_directory(codex_home.parent, "CODEX_HOME parent")
        codex_home.mkdir(mode=0o700)
        auth_mode = os.environ.get("MONAD_CODEX_AUTH", "isolated")
        if auth_mode not in {"isolated", "host-chatgpt"}:
            raise RuntimeError("MONAD_CODEX_AUTH must be isolated or host-chatgpt")
        if auth_mode == "host-chatgpt":
            host_home = Path(os.environ.get("MONAD_HOST_CODEX_HOME", str(Path.home() / ".codex"))).expanduser().resolve()
            auth_file = host_home / "auth.json"
            if auth_file.is_symlink() or not auth_file.is_file():
                raise RuntimeError("host ChatGPT auth file is missing or not a regular file")
            # Explicitly share the credential file. This is not a security
            # boundary: same-UID processes can read it and Codex may update it.
            os.symlink(auth_file, codex_home / "auth.json")
        writable_directory(codex_home, "CODEX_HOME", create=False)
        scratch = Path(tempfile.mkdtemp(prefix="monad-supervisor-", dir=TEMP_ROOT))
        runtime_dir = scratch / "runtime"
        runtime_dir.mkdir(mode=0o700)
        record["cli_executable"] = codex_bin
        ver = run_version(codex_bin, min(10, max(.1, deadline-time.monotonic())))
        if ver.returncode or ver.stdout.strip() != "codex-cli 0.164.0": raise RuntimeError(f"Codex CLI version unavailable or mismatched: {ver.stdout.strip()}")
        sandbox_mode = os.environ.get("MONAD_CODEX_SANDBOX", "workspace-write")
        if sandbox_mode not in {"workspace-write", "read-only"}:
            raise RuntimeError("MONAD_CODEX_SANDBOX must be workspace-write or read-only")
        argv = [codex_bin, "exec"]
        if sandbox_mode == "read-only":
            argv.extend(["--sandbox", "read-only"])
        else:
            argv.append("--approve-for-me")
        if auth_mode != "host-chatgpt":
            argv.extend(["--output-last-message", str(report)])
        argv.append(m["prompt"])
        run_env = os.environ.copy()
        run_env.update({"CODEX_HOME": str(codex_home), "HOME": str(codex_home), "XDG_CONFIG_HOME": str(codex_home / "config"), "XDG_CACHE_HOME": str(codex_home / "cache"), "XDG_DATA_HOME": str(codex_home / "data"), "XDG_STATE_HOME": str(codex_home / "state"), "XDG_RUNTIME_DIR": str(runtime_dir), "TMPDIR": str(scratch), "TMP": str(scratch), "TEMP": str(scratch), "PYTHONDONTWRITEBYTECODE": "1"})
        with (open(os.devnull, "wb") if auth_mode == "host-chatgpt" else log.open("xb")) as f:
            record["process_launch_state"] = "STARTING"
            atomic_json(state_path, state)
            proc = subprocess.Popen(argv, cwd=ROOT, env=run_env, stdout=f, stderr=subprocess.STDOUT, start_new_session=True)
            record.update({"process_id": proc.pid, "process_group": proc.pid, "process_started_at": utc(), "process_launch_state": "STARTED"})
            atomic_json(state_path, state)
            stopped = False
            timed_out = False
            while proc.poll() is None:
                if (BASE / "stop.request").exists():
                    stopped = True
                    os.killpg(proc.pid, signal.SIGTERM); break
                if time.monotonic() >= deadline:
                    timed_out = True
                    os.killpg(proc.pid, signal.SIGTERM); break
                allowed_links = {}
                if auth_mode == "host-chatgpt": allowed_links[codex_home / "auth.json"] = str(auth_file)
                live_bytes, live_unsafe = tree_output_bytes(run_dir, codex_home, scratch, allowed_symlinks=allowed_links, allowed_symlink_validator=arg0_symlink_validator(codex_home, bundled_codex_target(codex_bin)))
                if live_bytes > m["limits"]["max_output_bytes"] or live_unsafe:
                    output_limit = True
                    os.killpg(proc.pid, signal.SIGTERM); break
                time.sleep(.2)
            try: rc = proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL); rc = proc.wait(timeout=5)
            if auth_mode != "host-chatgpt":
                f.flush(); os.fsync(f.fileno())
        residual = terminate_group(proc) if proc else False
        if auth_mode == "host-chatgpt":
            try: (codex_home / "auth.json").unlink()
            except FileNotFoundError: pass
        after = snapshot()
        out_of_scope = sorted(p for p in before.keys() | after.keys() if not p.startswith("@@") and before.get(p) != after.get(p) and not any(p == x or p.startswith(x.rstrip("/") + "/") for x in m["allowed_paths"]))
        prot = protected_changes(before, after)
        test_results = []
        for test_num, t in enumerate(m["tests"]):
            if stopped or timed_out or output_limit or residual or (BASE / "stop.request").exists(): break
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                test_results.append({"command": t, "exit_code": None, "status": "TIMEOUT", "output": "shared milestone deadline exhausted"}); break
            test_log = scratch / f"test-{test_num+1}.log" if auth_mode == "host-chatgpt" else run_dir / f"test-{test_num+1}.log"
            test_proc = None
            try:
                with test_log.open("xb") as out:
                    test_env = os.environ.copy()
                    test_home = scratch / "test-home" if auth_mode == "host-chatgpt" else codex_home
                    if auth_mode == "host-chatgpt":
                        test_home.mkdir(mode=0o700)
                        for secret_var in ("OPENAI_API_KEY", "MONAD_HOST_CODEX_HOME", "MONAD_CODEX_HOME"):
                            test_env.pop(secret_var, None)
                    test_env.update({"CODEX_HOME": str(test_home), "HOME": str(test_home), "XDG_CONFIG_HOME": str(test_home / "config"), "XDG_CACHE_HOME": str(test_home / "cache"), "XDG_DATA_HOME": str(test_home / "data"), "XDG_STATE_HOME": str(test_home / "state"), "XDG_RUNTIME_DIR": str(runtime_dir), "PYTHONDONTWRITEBYTECODE": "1", "TMPDIR": str(scratch), "TMP": str(scratch), "TEMP": str(scratch)})
                    test_proc = subprocess.Popen(t, cwd=ROOT, env=test_env, shell=False, stdout=out, stderr=subprocess.STDOUT, start_new_session=True)
                    while test_proc.poll() is None and time.monotonic() < deadline and not (BASE / "stop.request").exists():
                        allowed_links = {}
                        if auth_mode == "host-chatgpt": allowed_links[codex_home / "auth.json"] = str(auth_file)
                        live_bytes, live_unsafe = tree_output_bytes(run_dir, codex_home, scratch, allowed_symlinks=allowed_links, allowed_symlink_validator=arg0_symlink_validator(codex_home, bundled_codex_target(codex_bin)))
                        if live_bytes > m["limits"]["max_output_bytes"] or live_unsafe:
                            output_limit = True
                            break
                        time.sleep(.1)
                    was_stopped = (BASE / "stop.request").exists()
                    was_timed_out = time.monotonic() >= deadline
                    was_output_limited = output_limit
                    if test_proc.poll() is None: test_residual = terminate_group(test_proc)
                    else: test_residual = terminate_group(test_proc)
                    out.flush(); os.fsync(out.fileno())
                output = test_log.read_text(errors="replace")
                summaries = list(re.finditer(r"^Ran ([0-9]+) tests? in [^\n]+$", output, re.M))
                count = int(summaries[-1].group(1)) if summaries else 0
                summary_tail = output[summaries[-1].end():] if summaries else ""
                unittest_ok = count > 0 and re.fullmatch(r"\s+OK\s*", summary_tail) is not None
                test_status = "TIMEOUT" if was_timed_out or was_stopped else ("FAIL" if was_output_limited else ("PASS" if test_proc.returncode == 0 and unittest_ok else "FAIL"))
                residual = residual or test_residual
                test_results.append({"command": t, "exit_code": test_proc.returncode, "status": test_status, "tests_run": count if summaries else None, "residual_process_group": test_residual, "output": "test output suppressed in host-chatgpt auth mode" if auth_mode == "host-chatgpt" else output[-4000:]})
                if was_stopped or was_timed_out or was_output_limited or test_residual: break
            except Exception as e:
                if test_proc: residual = terminate_group(test_proc) or residual
                test_results.append({"command": t, "exit_code": None, "status": "UNQUALIFIED", "output": str(e)})
                break
        stop_now = (BASE / "stop.request").exists()
        after = snapshot()
        out_of_scope = sorted(p for p in before.keys() | after.keys() if not p.startswith("@@") and before.get(p) != after.get(p) and not any(p == x or p.startswith(x.rstrip("/") + "/") for x in m["allowed_paths"]))
        prot = protected_changes(before, after)
        repository_identity_changed = any(before.get(k) != after.get(k) for k in ("@@HEAD", "@@branch"))
        plan_digest_after = digest(PLAN) if PLAN.is_file() else None
        plan_changed = plan_digest_after != auth["plan_sha256"]
        if auth_mode == "host-chatgpt":
            atomic_json(report, {"result": "CLI output suppressed in host-chatgpt auth mode", "cli_exit_code": rc})
            log.write_text("CLI output suppressed in host-chatgpt auth mode\n")
        report_ok = report.is_file() and report.stat().st_size > 0
        log_ok = log.is_file() and log.stat().st_size > 0
        changed_count = len([p for p in before.keys() | after.keys() if not p.startswith("@@") and before.get(p) != after.get(p)])
        allowed_links = {}
        if auth_mode == "host-chatgpt": allowed_links[codex_home / "auth.json"] = str(auth_file)
        output_bytes, unsafe_outputs = tree_output_bytes(run_dir, codex_home, scratch, allowed_symlinks=allowed_links, allowed_symlink_validator=arg0_symlink_validator(codex_home, bundled_codex_target(codex_bin)))
        limits_exceeded = changed_count > m["limits"]["max_changed_paths"] or output_bytes > m["limits"]["max_output_bytes"] or unsafe_outputs
        failed = rc != 0 or not report_ok or not log_ok or residual or not test_results or any(t["status"] != "PASS" or not t["tests_run"] for t in test_results) or out_of_scope or prot or stop_now or timed_out or output_limit or repository_identity_changed or plan_changed or limits_exceeded
        final_status = "BLOCKED" if out_of_scope or prot or repository_identity_changed or plan_changed else ("UNQUALIFIED" if failed else "PASS")
        if residual: final_status = "RUNNING"
        record.update({"finished_at": utc(), "exit_code": rc, "report_present": report_ok, "log_complete": log_ok, "residual_process_group": residual, "after": after, "out_of_scope": out_of_scope, "protected_changes": prot, "repository_identity_changed": repository_identity_changed, "plan_sha256_after": plan_digest_after, "plan_changed": plan_changed, "plan_sha256": auth["plan_sha256"], "tests": test_results, "status": final_status, "cli_version": ver.stdout.strip(), "timeout": timed_out, "output_limit_exceeded": output_limit, "stop_requested": stopped or stop_now, "changed_paths": changed_count, "output_bytes": output_bytes, "unsafe_outputs": unsafe_outputs, "limits_exceeded": limits_exceeded, "codex_home": str(codex_home), "temp_directory": str(scratch)})
        if record["status"] == "PASS": auth["next_index"] = idx + 1
        atomic_json(state_path, state); append_event(BASE / "events.jsonl", {"event": "finish", "id": rid, "status": record["status"], "at": utc()})
        print(record["status"]); return 0 if record["status"] == "PASS" else 2
    except Exception as e:
        residual = terminate_group(proc) if proc else False
        if isinstance(e, ProcessGroupFailure):
            residual = True
            record.update({"process_id": e.pid, "process_group": e.pid, "process_launch_state": "VERSION_CHECK_RESIDUAL"})
        try: after_error = snapshot()
        except Exception as snapshot_error: after_error = {"snapshot_error": str(snapshot_error)}
        record.update({"finished_at": utc(), "status": "RUNNING" if residual else "UNQUALIFIED", "error": str(e), "residual_process_group": residual, "after": after_error})
        atomic_json(state_path, state); append_event(BASE / "events.jsonl", {"event": "error", "id": rid, "error": str(e), "at": utc()})
        print(f"UNQUALIFIED: {e}"); return 2
    finally:
        if os.environ.get("MONAD_CODEX_AUTH", "isolated") == "host-chatgpt" and "codex_home" in locals():
            auth_entry = codex_home / "auth.json"
            try:
                if auth_entry.is_symlink() or auth_entry.is_file(): auth_entry.unlink()
            except OSError:
                pass
        if scratch is not None and not residual:
            try: shutil.rmtree(scratch)
            except OSError: pass
        fcntl.flock(lock_fd, fcntl.LOCK_UN); os.close(lock_fd)


def main() -> int:
    p = argparse.ArgumentParser(); p.add_argument("--plan-file", help="use a reviewed plan file (also MONAD_CODEX_PLAN_FILE)"); sub = p.add_subparsers(dest="action", required=True)
    for name in ("plan", "status", "report", "resume", "stop"): sub.add_parser(name)
    pf = sub.add_parser("preflight"); pf.add_argument("--codex-bin", help="Codex executable path or PATH-resolved command")
    ap = sub.add_parser("authorize-plan"); ap.add_argument("milestones", nargs="+")
    run = sub.add_parser("run"); run.add_argument("milestone"); run.add_argument("--timeout", type=int); run.add_argument("--codex-bin", help="Codex executable path or PATH-resolved command (also MONAD_CODEX_BIN)")
    args = p.parse_args()
    if args.plan_file:
        global PLAN
        PLAN = Path(args.plan_file)
    return command(args)

if __name__ == "__main__": sys.exit(main())
