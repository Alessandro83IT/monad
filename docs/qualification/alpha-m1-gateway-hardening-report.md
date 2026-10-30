# Alpha M1 Gateway Hardening Report

**Review date:** 2026-10-21  
**Disposition:** **READY FOR ADMIN REVIEW** — candidate only; not installed or activated.  
**Alpha M1 qualification:** incomplete. No privileged campaign was run.

## Scope and preserved state

The repository was on branch `main` at `abd6bac4b368427b76f6c98522afde928b418939` when this work began. The working tree already contained modified tracked files, untracked qualification evidence, build directories and project/configuration files. Those were inspected and preserved; no cleanup, commit, push, privileged test, `sudo`, installation, or sudoers modification was performed. The final tree includes this hardening work alongside that pre-existing work.

The repository's Codex project config enables native agents and defines Core, Verification, Security and Operations profiles. The configured profiles were inspected and native subagents were used for runner, test inventory, operations/hash and independent Security reviews. The local `codex` executable is unavailable (`command -v codex` found none), so this host's CLI version and actual runtime loading could not be verified. The checked-in conventions match documented Codex project conventions: [subagents](https://developers.openai.com/codex/multi-agent/) and [configuration reference](https://developers.openai.com/codex/config-reference/). Runtime compatibility remains to be checked on the Debian VM with its installed CLI.

## Diagnosis and corrections

The runner previously had a verification/use gap: hashing a source pathname did not pin the inode later executed, so a writable source path could be replaced after verification. Timeout handling could also confuse a timeout or stop request with confirmed termination, risking reuse of a fixture still in use. Evidence digests and same-host root ownership could be overread as immutability. Installer backup and rollback needed a complete inventory check and more explicit durability and recovery state.

The runner now pins source objects by file descriptor, rejects symlinks and unexpected object types, validates identity before/after copying, verifies the staged bytes and records object identity and hashes in a manifest. It publishes a completed stage atomically into a root-owned private directory and executes staged artifacts. A global runner lock and fixture lock prevent overlapping use. The runner separates deadline expiry, stop request, unit result, and confirmed termination; it requires an inactive/failed unit, zero main PID, empty cgroup, and a reaped client before declaring termination confirmed. If that cannot be established, it reports uncertainty and preserves the stage/fixture from reuse.

Reports use a run ID, schema version, output digest and integrity sidecars. The verifier detects changes relative to the recorded digest. Backup rollback validates the full manifest and inventory before changing targets, checks ownership/mode/hash, restores files atomically one at a time, and fsyncs files and containing directories at the recorded durability boundaries. The installer records recovery state before promotion. Multi-file promotion and rollback remain sequential, not globally atomic.

Authorization is a closed allowlist with strict argument validation. No arbitrary shell execution is exposed through the gateway. Build/test activities remain unprivileged; approved privileged operations are invocable only through the verified gateway. The installer does not automatically execute user-writable artifacts as root.

## Threat model and remaining limits

- **Unprivileged user:** root-owned mode-restricted staging, descriptor-relative operations, identity checks, symlink rejection, locks and hashes prevent or detect the addressed substitutions, subject to correct kernel/filesystem/systemd behavior and protected parent directories.
- **Accidental damage:** manifests, backup inventory, fsync, prevalidation and post-restore checks detect many incomplete or altered states and support recovery.
- **Alteration detection:** report and artifact hashes detect changes only when compared to an independently retained expected digest. A sidecar stored alongside a report is not an independent trust anchor.
- **Compromised root:** root can alter runner state, logs, reports, manifests and their hashes on the same host. No same-host root immutability is claimed. External audit retention or a separate trust domain is needed for stronger evidence.
- **Interrupted administration:** file-level atomic renames and fsync do not make a multi-file promotion/rollback transactional. A failure between renames can leave partial state; `install-started` enables recovery, which remains an explicit, reviewed administrative procedure.
- **Unverified privileged behavior:** no real root/systemd/cgroup/canary/fixture campaign was run. Static review and mocks are not proof of privileged runtime behavior.

## Verification

All checks below were unprivileged and used isolated build/output locations. No privileged CTest labels were run.

- Fresh isolated CMake/Ninja Debug configure and full build: **passed** (280 build steps).
- CTest excluding the `privileged` label: **128 selected, 100% passed, 0 failed; 11 declared skips**. The separate privileged label inventory contained 18 tests; none were run.
- Gateway hardening regressions: **13/13 passed**, including concurrent replacement, symlink/traversal, identity/hash/owner/mode, interrupted staging, blocked process and uncertain systemd termination, evidence/backup tampering, simulated restore and authorization.
- Installer static checks: **7/7 passed**. Autonomous infrastructure checks: **6/6 passed**. Orchestrator checks: **5/5 passed**. Canary child checks: **5/5 passed**. Qualification driver checks: **7/7 passed**.
- `bash -n qualification/install-athena-alpha-m1-admin.sh`: **passed**. `git diff --check`: **passed**.
- The C++ build emitted an existing `nodiscard` warning in `tests/store_metadata_errors_test.cpp`; it did not fail the build.

Independent Security review examined the integrated runner/support and then the final installer durability changes. It confirmed the source/stage pin alignment, timeout/client/unit confirmation behavior, backup inventory/ownership checks and fsync points. It found no new static issue in the final reviewed changes. It explicitly retained the limits above, especially partial multi-file transactions and the absence of privileged runtime proof.

## Current component hashes

Recalculate and compare these hashes at promotion time; the installer pins the runner, support library, sudoers draft and canary values below.

| Component | SHA-256 |
|---|---|
| `qualification/athena-alpha-m1-runner.py` | `2290b78c55c72489c0e7dc3bb1c1fedbc573e6bc605e690b6afd3a8ab326b24c` |
| `qualification/gateway_support.py` | `86be38660154161b6f71063567241fed3fbe1ef270d04a73b7c00a69126a9d28` |
| `qualification/athena-alpha-m1-systemd-canary.py` | `0c2567320d3f5c461d0c9a4a19beaf62b83615c511c28e699810bbdfa49f3107` |
| `qualification/athena-alpha-m1-sudoers` | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` |
| `qualification/install-athena-alpha-m1-admin.sh` | `55f13e8d1d91e149b6f24a8257fa48339f2b48e2a1b619f5ae0a5b1053d463a8` |
| `qualification/run-alpha-m1-privileged.py` | `20a87b8fc6f2f8cf55bb153b4fcc9362f937a97857e894866e0d05963d8ea83e` |

## Promotion and rollback

Promotion is a separate administrative action requiring explicit human approval. Before it, review this report and the complete diff, verify all hashes independently, review the exact sudoers draft with `visudo`, confirm the Debian VM/systemd/cgroup prerequisites and approved fixture state, and follow the staged procedure in `alpha-m1-admin-staging.md`. An authorized administrator must perform installation; this report grants no authorization. Do not treat this candidate as Alpha M1 qualified until the complete privileged campaign and evidence review succeed.

For rollback, stop and prove the relevant unit inactive with PID zero and an empty cgroup; preserve evidence and the fixture. Use only the installer's validated backup manifest and explicit rollback path after reviewing the exact targets. Validate the complete backup inventory before any restore, then verify restored owner/group/mode/hash and record results. Rollback mutates administrative state and requires explicit approval. If recovery state is partial or any identity/hash check fails, stop and escalate to an administrator; do not force-delete evidence or backups.

## Approval boundary and decision

Still requiring explicit administrator approval: gateway installation/promotion; any sudoers change; privileged campaign execution; gateway/security-policy changes; deletion of evidence, fixtures or backups; administrative rollback; privilege expansion; push or release. No such action was performed here.

**Decision: READY FOR ADMIN REVIEW.** This means the local candidate and non-privileged verification are ready for review and controlled promotion planning. It does **not** mean installed, activated, proven under root, or Alpha M1 qualified.
