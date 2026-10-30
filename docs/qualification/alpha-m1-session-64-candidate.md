# Alpha M1 — Session 64 supervisor candidate

**Session 64 supervisor prototype: PASS WITH CONDITIONS. Alpha M1 remains
BLOCKED.** This work delivers a synthetic-only local Codex milestone
supervisor. It does not qualify, activate, or operate Alpha M1.

## Architecture and implementation

`tools/codex-supervisor/supervisor.py` implements `plan`, `authorize-plan`,
`run`, `status`, `report`, `resume`, and `stop`. `milestones.json` is a strict,
declarative plan with a synthetic-only milestone, allowed paths, required
tests, timeout, dependencies, success checks, and stop conditions. Ordered
plan authorization is explicit and binds IDs to the plan file's SHA-256. The
runner accepts only the next authorized milestone and advances only after
PASS; it does not automatically launch the next milestone.

Each run gets a UUID directory, full stdout/stderr log, separate Codex final
response, and structured state record. State uses same-directory atomic
replacement with file and directory `fsync`; JSONL events are appended and
fsynced. An advisory lock prevents concurrent run/authorization. Stop requests
use a separate atomic marker observed during execution. The runner validates
Codex version as exactly 0.164.0, uses `--approve-for-me` without a sandbox
bypass, limits execution to the declared budget, records Git file hashes,
branch and `HEAD`, checks path changes, required tests, output/log presence,
stop/timeout, exit status, and residual process groups. Ambiguous `RUNNING`
records are not resumed automatically.

The test executable is a synthetic Python fixture. No real Codex session or
real milestone chain was launched.

The upstream `rust-v0.164.0` source defines `--approve-for-me` in shared
options and `--output-last-message` in the exec CLI options ([shared flags](https://github.com/openai/codex/blob/rust-v0.164.0/codex-rs/utils/cli/src/shared_options.rs),
[exec flags](https://github.com/openai/codex/blob/rust-v0.164.0/codex-rs/exec/src/cli.rs)).
The local `codex` binary is absent, so the command's behavior was not tested
against an installed release executable.

## Verification

| Check | Result | Detail |
|---|---|---|
| Synthetic supervisor unittest suite | **PASS — 28 tests** | `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/codex-supervisor -p 'test_*.py' -v` |
| Milestone success fixture | **PASS** | Exit 0, nonempty report/log, successful declared test, in-scope state |
| Failure/recovery fixtures | **PASS** | Nonzero exit, missing report, failed/fabricated test summary, out-of-scope/protected edit (including filename spaces), timeout, stop before/during Codex and verification, crash-state ambiguity, lock collision, corrupt state/cursor, changed plan before/during run, branch change, Codex/test child groups, empty log, version mismatch and unauthorized advance rejected as expected |
| Codex CLI 0.164.0 availability/options | **PARTIAL / NOT RUN** | Release-tag source confirms `--approve-for-me` and `--output-last-message`; local binary absent, so runtime parser/version check and real execution were not run |
| Broader Monad CTest/build | **NOT RUN** | Unrelated to this Python-only prototype; pre-existing build directories preserved |
| Privileged, collector, D6, installation, historical corpus operations | **SKIP / PROHIBITED** | No access, execution, or modification performed |
| Test failures/timeouts | **FAIL: 0; TIMEOUT: 0** | Synthetic suite completed successfully |

The initial and final Git branch is `main`; initial `HEAD` was
`abd6bac4b368427b76f6c98522afde928b418939` and final `HEAD` is unchanged. Final
status remains `main...origin/main` with the pre-existing tracked edits,
untracked evidence, build directories, and caches, plus only this session's
supervisor files and candidate report. The worktree was already heavily
modified with untracked reports, scripts, qualification artifacts, caches, and
build directories. Those pre-existing items were preserved. No historical
digest or protected corpus was recomputed or promoted. The supervisor itself
does not constitute an isolation boundary: Git status can miss ignored,
transient, reverted, or out-of-repository changes, and Codex receives the
workspace permissions of its host. Same-UID actors can rewrite or roll back
local state/logs, bypass advisory locks, and remove the workspace-local stop
marker. Atomic rename/fsync does not prove power-loss durability on an
unqualified storage stack. A process that calls `setsid()` can escape
process-group cleanup. The version string is not binary authentication.

## Independent reviews

Core, Security, and Operations reviews were requested separately. Initial
review findings included plan authorization not bound to content, concurrent
state races, a quoted unittest discovery pattern, incomplete process cleanup,
and missing explicit limitations. The implementation was revised to bind the
plan digest, serialize run and authorization with a lock, isolate stop in a
request marker, require a reported nonzero unittest count, clean/check process
groups before post-run snapshots, record branch/HEAD/PID/PGID, share one
deadline, stop verification processes, and document Git/same-UID/durability
limits. Follow-up Operations, Core, and Security reviews confirmed the
recovery/deadline, plan-integrity, and repository-identity fixes. Security
identified and prompted a final strict-state correction for boolean cursors;
that correction has a dedicated regression test.

## Changed artifacts and SHA-256

Digests are calculated from the final working-tree bytes after review fixes.
They are local integrity references, not promoted pins.

| Artifact | SHA-256 |
|---|---|
| `tools/codex-supervisor/supervisor.py` | `60cefdfad541cc4b1bab3fd424da7aaff3f55d6df2b5a91d2ddb1b5824c9be68` |
| `tools/codex-supervisor/milestones.json` | `d3a40f7b68f1d22ab9a2d8e9bd94cea1daebe5f1219ae94afe186892cabce391` |
| `tools/codex-supervisor/test_supervisor.py` | `3ddeb6c58cd5ccdcf5c6f74b5eef0d1d1e8eb203c01a9d323be9a00e84e889b2` |
| `tools/codex-supervisor/README.md` | `5c5c9d53593fb808ebffbcaa73779ae172e4643f6771d09a7f118dee8fbbb44a` |
| `tools/codex-supervisor/__pycache__/supervisor.cpython-313.pyc` | `f8d80b85c552a98f1bd2ac983b585b611b23e0727f79546a17e3cc0257dcb5cf` |
| `tools/codex-supervisor/__pycache__/test_supervisor.cpython-313.pyc` | `0e3208cf594cb7a54917db0892cfd615f5d01ed6297dd66178ff07b5e7d17dc1` |
| `docs/qualification/alpha-m1-session-64-candidate.md` | self-referential; reported separately in completion record |

The two bytecode cache files were created by an early test import before the
final source corrections. They are retained, are not qualification evidence,
and current Python source timestamps cause the updated source to be used.

## Residual limits and Session 65 proposal

This is a local convenience supervisor, not an authorization or security
control. The plan authorization is an unsigned local state entry; it proves
neither operator identity nor external approval. `resume` intentionally
refuses all incomplete runs and does not identify/terminate stale processes
automatically. Git auditing misses ignored files and reverted/transient edits.
The Codex 0.164.0 invocation could not be validated against an installed CLI.
Run/test subprocess cleanup and fsync semantics remain platform/filesystem
dependent; no hostile same-UID isolation is claimed. Historical protected
digests and the Alpha M1 evidence corpus were not accessed.

Proposed Session 65: on an isolated disposable development checkout, validate
this exact CLI invocation against an installed, pinned Codex CLI 0.164.0 using
a harmless synthetic fixture; review how the prompt/report/exit status behave,
then assess detached-process limits and explicit ignored-file inventory.
Do not enable real chained milestones or operational Alpha M1 work. Do not
start Session 65 automatically.

**Alpha M1 status: BLOCKED.**
