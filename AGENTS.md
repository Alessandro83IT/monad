# Monad development instructions

Monad is a C++20 package manager built with CMake and tested with CTest.

## Working rules

- Inspect `git status`, branch, and `HEAD` before changing files. Treat all pre-existing edits, untracked evidence, and build directories as user work. Never reset, clean, overwrite, or delete them.
- Keep changes scoped to the request. Do not commit or push unless explicitly asked.
- Use isolated build directories. Do not run privileged tests or invoke `sudo` as part of ordinary development.
- Run targeted CTest cases for changed behavior; run broader suites when practical. Preserve and report PASS, FAIL, SKIP, TIMEOUT, and NOT RUN separately.
- Do not claim Alpha M1 qualification until the approved Debian VM campaign has completed every required privileged case and evidence review.
- Preserve the Alpha M1 D6 residual fixture and all logs, receipts, reports, and backups. Never change installed administrative paths from a development session.
- Keep security-sensitive pins synchronized only through an explicitly reviewed promotion workflow. A source edit that invalidates an installer pin must be called out; do not silently regenerate the pin.

## Delegation

For substantial independent work, use the project agents in `.codex/agents/`: `core`, `verification`, `security`, and `operations`. Give each agent bounded paths and a concrete deliverable. Avoid concurrent edits to the same paths. The coordinator owns integration, final review, and reporting.

## Administrative boundary

The repository and build outputs are untrusted inputs. Never grant general root shell, execute workspace binaries as root, or add wildcard sudo rules. Root operations require a separately reviewed, root-owned, hash-pinned artifact and exact argument allowlist. Human approval is required for sudoers or gateway installation/changes, privilege expansion, security-control changes, destructive cleanup, state-changing rollback, push, and release.

See `docs/development/autonomous-infrastructure.md`, `docs/security/authorization-policy.md`, and `docs/qualification/alpha-m1-recovery.md` before changing administrative or qualification workflows.
