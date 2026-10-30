# Alpha M1 — Session 68 candidate

**M2 supervisor auth integration: synthetic PASS with limits. Real Codex test: NOT RUN. Alpha M1: BLOCKED.**

## Architecture

The default remains isolated auth: the supervisor creates a fresh per-run `CODEX_HOME`, `HOME`, XDG directories, and temp directory. `MONAD_CODEX_AUTH=host-chatgpt` is an explicit opt-in for an operator-provided staged profile. `MONAD_HOST_CODEX_HOME` names the directory containing that profile (default `~/.codex`, which must not be used when the active host auth file must remain unchanged). The supervisor does not copy credentials. It validates that the staged auth path is a regular non-symlink file and links it into the isolated per-run Codex home. The link is removed after the Codex process group exits. `config.toml` is not read, copied, linked, or modified; isolated Codex defaults apply.

Codex stdout/stderr are discarded in this mode, and `--output-last-message` is omitted. Retained run log/report contain a fixed non-secret marker. Required tests run with a separate credential-free home, scrub API-key/staged-home environment variables, write output only to scratch, and retain only status/count metadata. State, logs, tests, temporary files, and Codex session state remain separate from the staged credential source.

## Threat model and limits

The staged credential file is intentionally shared with the Codex process. Codex can read it and may update it; same-UID processes can also read or replace it. A symlink and file mode are not security boundaries. The source path check is validation only and has a same-UID race. Therefore a real run must point `MONAD_HOST_CODEX_HOME` at a manually staged disposable profile, never the active host profile. The supervisor does not create that copy automatically. The operator must protect and later remove the staged copy according to host policy.

Output suppression prevents normal CLI output from being retained in supervisor logs/reports, but cannot guarantee that a credential never enters Codex-managed session state, process memory, crash output outside the supervisor, or host telemetry. The run home and same-UID workspace remain untrusted inputs; Git snapshots and path allowlists are detection, not containment. No test in this session accessed a real credential. Manual inspection on a disposable Debian environment remains a prerequisite before considering the mode operationally qualified.

## Test results

| Check | Result | Evidence |
|---|---|---|
| Supervisor synthetic unit suite | **PASS — 51 tests** | `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/codex-supervisor -p 'test_*.py' -v`, exit 0. Includes simulated auth source, output suppression, no source-file mutation, and credential-free test home. |
| CLI version/preflight on this host | **NOT RUN** | No real Codex command was invoked. |
| Real host-chatgpt auth run | **NOT RUN** | Explicitly deferred to Debian shell procedure below. |
| CTest / Monad C++ suite | **SKIP** | Python-only change. |
| Sudo, privileged tests, collector, D6 payload, installation, baseline/pin promotion, commit, push, chained session | **SKIP** | Not performed. |
| Alpha M1 qualification | **BLOCKED** | No approved Debian VM privileged campaign was run. |

During implementation, an intermediate auth-mode fixture run was correctly blocked because its argv capture wrote inside the synthetic repository outside the declared scope. The fixture was moved to its isolated supervisor-state directory; the targeted auth-mode test and final 51-test suite then passed. No project worktree path was changed by that fixture.

## Independent reviews

- **Core — PASS with limits.** Confirmed credential-free test home/output handling, auth-link cleanup, and that `MONAD_CODEX_SANDBOX=read-only` maps to `--sandbox read-only` without `--approve-for-me`. The CLI sandbox does not cover the supervisor's separately invoked tests.
- **Security — PASS with limits.** Confirmed no retained CLI response/stdout, staged-only auth source in the procedure, and documented same-UID/path races plus possible Codex-managed state. The CLI sandbox does not sandbox supervisor-launched Python tests. Real CLI behavior remains unverified.
- **Operations — PASS.** Confirmed the protected qualification path, read-only CLI sandbox, unique external state/temp paths, manual staged profile, post-run staged-auth-only removal, and retention of evidence. The real Codex/auth path remains NOT RUN.

The Debian shell test remains a candidate procedure, not approval to claim Alpha M1 qualification.

## Changed files

- `tools/codex-supervisor/supervisor.py`
- `tools/codex-supervisor/test_supervisor.py`
- `tools/codex-supervisor/README.md`
- `tools/codex-supervisor/session68-host.sh`
- `tools/codex-supervisor/test_session68_host.py`
- `docs/qualification/alpha-m1-session-68-candidate.md`

No historical baseline, existing state/log, D6 fixture, build directory, or unrelated worktree edit was intentionally changed.

## SHA-256

Candidate artifact hashes (not promoted security pins):

| Artifact | SHA-256 |
|---|---|
| `tools/codex-supervisor/supervisor.py` | `7ad66ef5d1663efd5ebe7f5abc5bd9f37e218e2e93165de47385295740f9b2bf` |
| `tools/codex-supervisor/test_supervisor.py` | `7a870ede4e5e62cacc1cb5dea494e76498e317287777f5237f13fafbf15447a7` |
| `tools/codex-supervisor/README.md` | `c208136bd7ebcda5c5893b218b649f22a53bad01f17dde1fad6002acf69000e1` |
| This report | Calculated after final save and reported in the session completion message (self-hash is not embedded). |

## Git and baseline

Initial and final state: branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`; no commit or push. At start, nine tracked CMake/test paths were modified, and many build, qualification, supervisor, and evidence paths were untracked. They were preserved. The Session 68 candidate is an additional untracked deliverable; the supervisor source, tests, and README were pre-existing untracked workpaths edited for this request. No historical baseline or installed administrative path changed.

## One-shot Debian host driver

The autonomous host procedure is `tools/codex-supervisor/session68-host.sh`.
It requires an already staged, disposable `auth.json`; it never stages or
copies a credential itself. Do not point it at the active `~/.codex` profile.
The script prepares isolated state and a unique plan, performs CLI preflight,
requires `--authorize`, records one authorization, and attempts exactly one
read-only run with the synthetic supervisor test suite. It then collects the
sanitized report/status and checks for residual process state. After confirmed
process absence it removes only the explicitly supplied staged auth file and
retains the plan, state, logs, reports, and other evidence in its unique `/tmp`
directory. On ambiguous process state, the credential is retained for manual
recovery. No automatic retry or resume is performed.

Example, from the repository root in a normal non-root Debian shell:

```sh
tools/codex-supervisor/session68-host.sh \
  --authorize \
  --staged-auth /absolute/path/to/disposable-profile/auth.json \
  --codex-bin /absolute/path/to/codex
```

The CLI's read-only sandbox is the execution restriction; the prompt is not
containment. Review `final-report.txt`, `supervisor-report.jsonl`, and the
supervisor status in the printed evidence directory. This remains a candidate
smoke procedure, not Alpha M1 qualification.

**Alpha M1 remains BLOCKED.**
