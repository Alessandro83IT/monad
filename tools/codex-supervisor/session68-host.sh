#!/usr/bin/env bash
# One-shot Session 68 host smoke. Requires an operator-staged disposable auth file.
set -Eeuo pipefail
umask 077

usage() {
  cat <<'EOF'
Usage: session68-host.sh --authorize --staged-auth FILE [--codex-bin PATH]

Runs exactly one Session 68 read-only smoke and its synthetic unittest suite.
FILE must be auth.json in a dedicated disposable staging directory, not ~/.codex.
The script never copies, prints, or reads credential contents. Evidence is kept
under a fresh /tmp directory; only FILE is removed after a verified process
exit. On ambiguity or residual processes, credentials are retained for recovery.
EOF
}

die() { printf 'BLOCKED: %s\n' "$*" >&2; exit 2; }
authorized=0 staged_auth= codex_bin="$(command -v codex || true)"
while (($#)); do
  case "$1" in
    --authorize) authorized=1; shift ;;
    --staged-auth) (($# >= 2)) || die 'missing --staged-auth value'; staged_auth=$2; shift 2 ;;
    --codex-bin) (($# >= 2)) || die 'missing --codex-bin value'; codex_bin=$2; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; die "unknown argument: $1" ;;
  esac
done
((authorized == 1)) || die 'explicit --authorize is required'
[[ -n "$staged_auth" && -n "$codex_bin" ]] || die 'staged auth file and Codex executable are required'
[[ "$staged_auth" = /* ]] || die '--staged-auth must be an absolute path'
[[ ! -L "$staged_auth" ]] || die '--staged-auth must not be a symlink'
[[ "$codex_bin" = /* ]] || codex_bin="$(command -v -- "$codex_bin" || true)"
[[ -n "$codex_bin" && -x "$codex_bin" ]] || die 'Codex executable is unavailable'

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"
staged_auth="$(realpath -e -- "$staged_auth")"
active_auth="$(realpath -m -- "${HOME:?}/.codex/auth.json")"
[[ "$staged_auth" != "$active_auth" ]] || die 'active host auth path is forbidden'
[[ "$(basename -- "$staged_auth")" == auth.json ]] || die 'staged credential must be named auth.json'
[[ -f "$staged_auth" && ! -L "$staged_auth" ]] || die 'staged auth must be a regular non-symlink file'
[[ "$(stat -c %a -- "$staged_auth")" == 600 ]] || die 'staged auth file must have mode 600'
staged_auth_identity="$(stat -c %d:%i -- "$staged_auth")"
staged_dir="$(dirname -- "$staged_auth")"
[[ "$staged_dir" != "$(realpath -m -- "${HOME}/.codex")" ]] || die 'active Codex home is forbidden'

session_root="$(mktemp -d "${TMPDIR:-/tmp}/monad-session68.XXXXXXXX")"
chmod 700 "$session_root"
mkdir -m 700 "$session_root/state" "$session_root/tmp"
export MONAD_SUPERVISOR_STATE_DIR="$session_root/state"
export MONAD_SUPERVISOR_TMPDIR="$session_root/tmp"
export MONAD_CODEX_AUTH=host-chatgpt
export MONAD_CODEX_SANDBOX=read-only
export MONAD_HOST_CODEX_HOME="$staged_dir"
export MONAD_CODEX_BIN="$codex_bin"
export MONAD_CODEX_PLAN_FILE="$session_root/plan.json"
unset OPENAI_API_KEY MONAD_CODEX_HOME

supervisor="$repo_root/tools/codex-supervisor/supervisor.py"
report_file="$session_root/final-report.txt"
completed=0
cleanup_credential=0
write_report() {
  local status=${1:-UNQUALIFIED} residual=${2:-unknown}
  {
    printf 'Session 68 host smoke: %s\n' "$status"
    printf 'Evidence directory: %s\n' "$session_root"
    printf 'Credential cleanup: %s\n' "$([[ $cleanup_credential == 1 ]] && echo REMOVED || echo RETAINED)"
    printf 'Residual process group: %s\n' "$residual"
    printf 'Codex nested invocation by driver: none\n'
    printf 'Alpha M1 qualification: BLOCKED\n'
  } >"$report_file"
}
on_exit() {
  local rc=$?
  trap - EXIT
  if ((completed == 1)) && [[ ! -e "$session_root/residual-process-group" ]]; then
    if [[ -f "$staged_auth" && ! -L "$staged_auth" && "$(stat -c %d:%i -- "$staged_auth" 2>/dev/null || true)" == "$staged_auth_identity" ]]; then
      if rm -- "$staged_auth"; then cleanup_credential=1; else rc=2; fi
    else
      printf 'Credential cleanup skipped: staged file identity changed.\n' >&2
      rc=2
    fi
  fi
  local status=UNQUALIFIED residual=not-verified
  [[ -f "$session_root/run-status" ]] && status="$(<"$session_root/run-status")"
  [[ -e "$session_root/residual-process-group" ]] && residual=yes || { ((completed == 1)) && residual=no; }
  write_report "$status" "$residual"
  printf 'Evidence retained at %s\n' "$session_root"
  exit "$rc"
}
trap on_exit EXIT

# Plan is constrained to a single, read-only request and one synthetic test command.
cat >"$MONAD_CODEX_PLAN_FILE" <<'PLAN'
{
  "schema": 1,
  "milestones": [{
    "id": "session68-readonly-smoke",
    "objective": "Read-only authentication and supervisor smoke qualification",
    "prompt": "Read tools/codex-supervisor/README.md and docs/qualification/alpha-m1-session-68-candidate.md. Return a brief assessment of documented isolation behavior and limits. Do not edit, create, delete, or run workspace commands or payloads. Do not quote or disclose credentials.",
    "allowed_paths": ["qualification/session68-readonly-smoke/"],
    "tests": [["python3", "-m", "unittest", "discover", "-s", "tools/codex-supervisor", "-p", "test_*.py"]],
    "timeout_seconds": 180,
    "limits": {"max_timeout_seconds": 180, "max_changed_paths": 1, "max_output_bytes": 16777216},
    "depends_on": [],
    "success": ["simulated CLI tests pass", "no protected artifacts change", "report exists"],
    "stop_conditions": ["CLI mismatch", "timeout", "stop request", "protected path change", "out-of-scope change"]
  }]
}
PLAN

cd -- "$repo_root"
python3 "$supervisor" plan
python3 "$supervisor" preflight --codex-bin "$codex_bin"
python3 "$supervisor" authorize-plan session68-readonly-smoke
set +e
python3 "$supervisor" run session68-readonly-smoke --timeout 180 --codex-bin "$codex_bin"
run_rc=$?
set -e
python3 "$supervisor" report >"$session_root/supervisor-report.jsonl"
python3 "$supervisor" status >"$session_root/supervisor-status.json"

# Read only status metadata; fail closed if the supervisor reports an active or residual group.
python3 - "$session_root/supervisor-status.json" "$session_root/residual-process-group" <<'PY'
import json, pathlib, sys
state = json.loads(pathlib.Path(sys.argv[1]).read_text())
active = [r for r in state.get("runs", []) if r.get("status") == "RUNNING" or r.get("residual_process_group")]
if active:
    pathlib.Path(sys.argv[2]).write_text("residual or ambiguous process state\n")
    raise SystemExit(2)
PY
residual_rc=$?
if ((residual_rc != 0)); then
  printf 'UNQUALIFIED\n' >"$session_root/run-status"
  exit 2
fi
completed=1
if ((run_rc == 0)); then printf 'PASS\n' >"$session_root/run-status"; else printf 'UNQUALIFIED\n' >"$session_root/run-status"; fi
exit "$run_rc"
