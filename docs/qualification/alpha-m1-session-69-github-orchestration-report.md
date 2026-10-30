# Session 69 — GitHub orchestration report

## Result

Implemented a bounded, read-only GitHub issue acquisition path and a local
approval workflow. Alpha M1 remains **BLOCKED**. No issue was fetched from live
GitHub, no GitHub write was attempted, no Codex process was launched, and no
service or installation was activated.

## Changes in scope

- `tools/codex-supervisor/github_orchestrator.py`: fixed-repository HTTPS GET,
  timeout and response/body bounds, strict response checks, snapshot writing,
  explicit local issue authorization, verification, and proposed-plan output.
- `tools/codex-supervisor/github_issue_adapter.py`: restrict snapshots to
  `Alessandro83IT/monad`; retain the issue/body hash gate and canonical
  Supervisor plan validation.
- `tools/codex-supervisor/test_github_orchestrator.py` and
  `tools/codex-supervisor/test_github_issue_adapter.py`: synthetic network,
  malformed response, repository policy, and authorization cases.
- `tools/codex-supervisor/README.md` and
  `docs/development/github-issue-orchestration.md`: commands, trust model,
  boundaries, limits, future activation requirements.

These paths were already untracked in the initial worktree status, along with
many other pre-existing user files. They remain untracked; no unrelated paths
were changed intentionally.

## Verification

- Python unittest discovery with `env -i` and `PYTHONDONTWRITEBYTECODE=1`:
  **PASS**, 85 tests (`Ran 85 tests`, `OK`). The expected argparse usage text
  appears in the test that verifies the offline adapter rejects omission of
  `--dry-run`.
- Python AST parsing for adapter, orchestrator, supervisor, and both test files:
  **PASS**.
- `git diff --check`: **PASS** for tracked diff. The target implementation and
  documentation are untracked in the initial worktree, so they are not covered
  by Git's ordinary diff check; an explicit trailing-whitespace scan is also
  recorded in the session result.
- Live GitHub acquisition: **NOT RUN** (no issue number was supplied and
  synthetic tests cover network behavior).
- Privileged tests, host Session 68 driver, nested Codex, installation,
  commit/push, and remote writes: **NOT RUN** by design.

## Security properties and residual risks

The fetch endpoint is fixed to GitHub's HTTPS API for the one allowed
repository. Requests are GET-only and carry no token. Timeouts, 300,000-byte
response cap, 262,144-byte body cap, content type, JSON duplicate keys,
repository URL, issue number/state/body, and pull request marker are checked.
Error messages suppress transport exception details. Snapshots and generated
files use exclusive creation and mode 0600.

Issue approval is local, same-user, unsigned, non-expiring, and reusable while
the body remains unchanged. It is not a defense against another process under
the same UID. HTTPS/repository identity does not establish author trust. The
issue body remains untrusted prompt text. The output is only a proposed plan;
the independent Supervisor `authorize-plan` and manual `run` actions remain
outside the orchestrator. The Supervisor remains a development control rather
than a security boundary against a hostile same-user process or kernel.
