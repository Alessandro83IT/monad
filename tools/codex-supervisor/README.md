# Local Codex milestone supervisor

This Python CLI stores its state and run evidence under `.codex-supervisor/`.
It is a development helper, not a security boundary and not part of Monad's
operational/Alpha M1 code.

```sh
python3 tools/codex-supervisor/supervisor.py plan
python3 tools/codex-supervisor/supervisor.py preflight --codex-bin /absolute/path/to/codex
python3 tools/codex-supervisor/supervisor.py status
python3 tools/codex-supervisor/supervisor.py report
python3 tools/codex-supervisor/supervisor.py authorize-plan supervisor-synthetic
python3 tools/codex-supervisor/supervisor.py run supervisor-synthetic --timeout 3600
python3 tools/codex-supervisor/supervisor.py stop
python3 tools/codex-supervisor/supervisor.py resume
```

`authorize-plan` is an explicit local operator action. It binds the selected
ordered milestone IDs to the SHA-256 of the complete plan file. A changed plan
requires a new authorization. Only the next listed milestone can run; a later
milestone is enabled after the previous one passes. No automatic chaining or
relaunch occurs. The authorization is not signed and does not prove who typed
the command; any process under the same UID can replace the plan and state.

Each run checks for exact Codex CLI version `0.164.0`, then invokes
`<codex-bin> exec --approve-for-me --output-last-message <report> <prompt>` with the
repository as working directory. It does not pass a sandbox bypass. The
`--approve-for-me` mode is not treated as a promise that all operations will be
approved. Session 64 could not runtime-verify this command because its CLI was
absent. Session 66 verified the installed CLI version and help, but nested
app-server initialization failed on a read-only filesystem. The version
string is a compatibility check, not binary authentication; the operator's `PATH` and
installed executable must be trusted separately. Select the executable with
`--codex-bin /absolute/path/to/codex` on `run` or `MONAD_CODEX_BIN`; by default
the `codex` command is resolved through the inherited `PATH`. This does not
modify `PATH`. A reviewed alternate plan can be selected with the global
`--plan-file PATH` option before the action or `MONAD_CODEX_PLAN_FILE`; its
digest remains part of authorization. The 0.164.0 release-tag source declares `--approve-for-me` for shared
interactive/exec options and `--output-last-message` on the exec command; see
the [versioned option definitions](https://github.com/openai/codex/blob/rust-v0.164.0/codex-rs/utils/cli/src/shared_options.rs)
and [exec options](https://github.com/openai/codex/blob/rust-v0.164.0/codex-rs/exec/src/cli.rs).

The supervisor records Git status file hashes, branch, and `HEAD` before and
after. It checks declared changed-path scope and a fixed set of protected
paths, requires a nonempty final response and log, checks required argv-based
tests (including that a unittest invocation reports at least one test), and
tracks timeout, stop request, process-group cleanup, and return code. A stop is
stored in a separate atomic request file so the active runner can observe it
without racing a stale state rewrite. Event records are append-only JSONL; run
state uses same-directory temporary files, `fsync`, rename, and directory
`fsync`. `flock` excludes concurrent run/authorization operations.
Changes to `HEAD`, branch, or the authorized plan digest classify the run as
BLOCKED.

Required test commands inherit `PYTHONDONTWRITEBYTECODE=1` so Python tests do
not alter existing bytecode caches in the repository.

## Host-orchestrated mode

## GitHub Issues orchestration (one-shot, read-only)

`github_orchestrator.py` fetches a single issue through a bounded HTTPS GET
from the fixed `Alessandro83IT/monad` repository. It performs no remote writes,
sends no credentials, and cannot start Codex. The issue must first receive
explicit local authorization bound to repository, number, and exact body hash.
`github_issue_adapter.py` validates that authorization and a single-milestone
Supervisor plan (`schema: 1`) using the same canonical `supervisor.load_plan()`
schema used by local plans.

See [GitHub issue orchestration](../../docs/development/github-issue-orchestration.md)
for commands, formats, threat model, and limits. `verify` and `prepare` emit or
write a proposed plan only. The operator must review it and separately use the
Supervisor's local `authorize-plan` command. The orchestrator never invokes
that command or starts a run.

The supervisor can be started directly from a Debian shell; it does not need a
parent Codex session. `preflight` resolves the configured CLI (including an
absolute path when `PATH` is restricted), checks exact version `0.164.0`, and
probes the state and temporary directories with exclusive throwaway files.
Use `--codex-bin` on `preflight` or `run`, or set `MONAD_CODEX_BIN`. Set
`MONAD_SUPERVISOR_STATE_DIR` and `MONAD_SUPERVISOR_TMPDIR` to select writable
locations. A run gets a fresh `CODEX_HOME` beneath its unique run directory and
a unique temporary directory; `HOME`, `XDG_*`, and temp variables are isolated
for Codex and tests. The inherited `CODEX_HOME` and host profile are not reused
by default. Authentication must be available through an environment-supported
mechanism; the supervisor does not copy credentials or existing profile files.
To opt in to ChatGPT authentication from a separately staged profile, set
`MONAD_CODEX_AUTH=host-chatgpt` and `MONAD_HOST_CODEX_HOME` to that profile's
directory. The default source path is `~/.codex`; do not use that default for a
real run when the host auth file must remain unchanged. The supervisor does not
copy credentials. The operator must stage a dedicated disposable auth file
manually before a real run. The supervisor links that file into the fresh
per-run `CODEX_HOME`, then removes the link when the CLI exits. Codex can read
and update the staged file; the link and same-UID permissions are not a
security boundary. `config.toml` is neither read nor changed; isolated Codex
defaults apply. In this mode raw CLI output and the CLI final response are
discarded, and retained log/report files contain only a fixed status marker.
The Codex sandbox mode can be selected with `MONAD_CODEX_SANDBOX=read-only`;
this passes `--sandbox read-only` and omits `--approve-for-me`. The default is
`workspace-write`, which retains the supervisor's existing `--approve-for-me`
behavior. The sandbox is the CLI's enforcement layer, while Git scope checks
remain detection only.
This cannot prevent credentials from appearing in other Codex-managed files,
process memory, or host-side telemetry. An
explicit `MONAD_CODEX_HOME` is accepted only when that path does not already
exist, and the created home is retained with the run evidence. It is a
single-use path and is not accepted for a later run after it exists.
Per-run temporary storage is removed after process-group absence is confirmed;
logs and reports remain in the state directory. The default Codex home remains
there too; an explicitly configured home remains at its configured path. A
killed supervisor or ambiguous process group may leave its unique temporary
directory behind for manual inspection.

Each declarative milestone includes `limits` for maximum timeout,
changed Git paths, and total run output bytes. The byte limit counts regular
files in the run directory, Codex home, and temporary tree (overlapping paths
count once). It is sampled during execution and checked again before cleanup;
this is a monitor, not a filesystem quota, and fast writes can exceed the
threshold between samples. Special files and unreadable directories make the
result unqualified. Symlinks are rejected unless they are one of two exact
runtime link facts: Codex's transient `CODEX_HOME/tmp/arg0-linux/codex-arg…/`
directory may contain only the known `codex`, `applypatch`, `apply_patch`, and
`codex-linux-sandbox` links, each targeting the executable bundled beside the
selected Linux Codex launcher; or host-chatgpt `CODEX_HOME/auth.json` may
contain the exact staged auth-file path. The monitor checks type with `lstat`
and compares the literal target with `readlink`; it never follows either
target. Matching only a link location or name is insufficient. The
authorization digest covers
the complete plan, including these limits. Changes to the plan require an
explicit new authorization. Preflight is advisory: the CLI version text is not
binary authentication, and Git snapshots do not contain or isolate the host.
The caller must arrange authentication through the normal host environment or
explicitly stage a disposable profile for the host-chatgpt mode above.

## Limits

Git status is only a detector. It can miss ignored files, transient changes,
files outside the repository, and modifications that are reverted before the
post-run snapshot. It does not isolate Codex. Codex runs with the workspace
permissions provided by its host environment; Git allowlists and prompt text
do not confine it. A same-UID process can replace state, logs, plan, lock, or
event history, restore older valid state, and bypass advisory locking. Local
`fsync`/rename behavior depends on the filesystem and storage stack; this work
does not qualify power-loss durability. The process-group check does not
contain a process that calls `setsid()` to escape its group or changes
credentials. Because the stop marker is inside the workspace, a Codex process
with workspace-write access could remove it; stop is a cooperative request,
not an external kill guarantee.

If the supervisor stops unexpectedly while a run is recorded `RUNNING`,
`resume` refuses to relaunch. It does not automatically signal a recorded PID,
because PIDs can be reused and local state is not authenticated. Inspect the
run evidence and live process identity manually; authorize a fresh plan only
after resolving the ambiguity. A kill failure remains `RUNNING` and blocks
subsequent runs.

The supplied plan and fixtures are synthetic-only. The Git perimeter does not
protect D6, historical evidence, installed administrative paths, or Alpha M1.
No collector, payload, privileged test, sudo, installation, digest promotion,
commit, push, or automatic next session is part of this tool.

## Session 68 one-shot Debian host driver

`session68-host.sh` automates the candidate Session 68 smoke as one bounded
sequence: it creates a fresh external state/temp root and one-milestone plan,
runs preflight, records one explicit plan authorization, starts one read-only
run, collects status/report, checks for a running or residual process group,
and removes only the explicitly supplied staged `auth.json` after process
absence is established. The state directory, plan, reports, logs, and all other
evidence stay in the printed `/tmp/monad-session68.*` directory.

First create a disposable credential profile using the host's approved
credential staging method. The script never copies or reads the credential
contents and rejects the active `~/.codex/auth.json` path. Invoke it directly
from a normal Debian shell, after reviewing the candidate procedure:

```sh
tools/codex-supervisor/session68-host.sh \
  --authorize \
  --staged-auth /absolute/path/to/disposable-profile/auth.json \
  --codex-bin /absolute/path/to/codex
```

`--authorize` is mandatory. The run is one-shot and has no retry/resume path.
If preflight fails, execution is not attempted. If process state is ambiguous,
the script retains the staged credential and evidence for manual recovery; it
does not clean or signal an ambiguous process. In other completed cases, only
the supplied credential file is unlinked. Review `final-report.txt` and
`supervisor-report.jsonl`; this smoke does not qualify Alpha M1. Synthetic
driver tests do not invoke Codex or access a host credential.
