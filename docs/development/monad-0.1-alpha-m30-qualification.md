# Monad 0.1 Alpha M30 forensic collector security closure

## Decision

**NO_GO** for production acquisition and Alpha M1 release. The M30 candidate corrects the M29 synthetic-root TOCTOU and closes several boundedness and output-race gaps. Synthetic tests pass, but this session did not run on the approved Debian VM and performed no privileged operation. `GO_FOR_OPERATOR_REVIEW` is not warranted until source review closes the issues called out below; this report does not authorize collector execution.

## Baseline and preservation

Inspected M29's collector, tests and closure report. Branch `main`, `HEAD` `abd6bac4b368427b76f6c98522afde928b418939`. The worktree was already dirty and contained substantial untracked evidence and build directories. This work added only the M30 script, test and report; M29 and all pre-existing work were preserved. No commit, cleanup, administrative action, network access, mount operation, or privileged acquisition was performed.

## Defects addressed

- **Synthetic root TOCTOU:** M29 checked a pathname and later rebuilt traversal paths from it. M30 opens each root component with `O_DIRECTORY|O_NOFOLLOW`, validates the final descriptor's owner, mode and type, and retains it. All fixture traversal and fault injection use descriptor-relative operations from that root. A deterministic test renames the validated root and installs a symlink at its old pathname; traversal remains on the opened root.
- **Descriptor lifetime and pressure:** directory and file descriptors use `finally` closes; traversal holds one directory descriptor per recursion level and is bounded by depth. Output uses an opened descriptor and checks its identity against the parent entry before and after writing. Synthetic low-`RLIMIT_NOFILE` plus deep-tree coverage confirms safe partial termination, but does not assert that every kernel/runtime combination reports `EMFILE` identically.
- **Enumeration and mutation:** snapshots stop at their cap; the global entry limit bounds traversal and snapshot work, with overflow recorded as incomplete. A second bounded snapshot and directory metadata comparison detect mutation within the observation window. The acquisition is not atomic; changes outside the observed window remain possible.
- **Output:** the destination is created with `mkdirat`, opened no-follow, and checked for type, effective owner, mode and `(device,inode)` identity. Evidence files use `O_EXCL|O_NOFOLLOW`, loop over short writes, fsync each file, then fsync the directory. A later replacement of the output name is detected; the command fails and preserves evidence on the original descriptor. Existing destinations and injected write/fsync faults are covered. A durability failure returns status 2 with a diagnostic; preserved files must not be represented as durable evidence.
- **Error and absence reporting:** required leaf absence, optional `/run/athena-alpha-m1` leaf absence, missing intermediate directories, traversal errors and identity/mount failures remain distinct. Historical targets are observations only; current absence does not establish historical absence. No broad exception handler hides programming defects.
- **Production invariants:** M30 retains M29's exact Debian VM identity constants, fixed acquisition allowlist, device and mount-ID checks, no-follow traversal, special-file refusal, metadata-only default, exact empty lock-file hash authorization, restrictive output mode and checksums.

## Source-derived path roles

The allowlist remains unchanged. Roles are based on `qualification/install-athena-alpha-m1-admin.sh` and `qualification/athena-alpha-m1-runner.py`; they describe the source contract, not proof of current or historical state.

| Path | Role | Basis / interpretation |
|---|---|---|
| `/usr/local/sbin/athena-alpha-m1-runner` | Required installed artifact | Installer verifies installed runner and pin. |
| `/usr/local/sbin/gateway_support.py` | Required installed artifact | Installer verifies support artifact. |
| `/etc/sudoers.d/athena-alpha-m1` | Required installed artifact | Installer validates and verifies sudoers output. |
| `/var/lib/athena-alpha-m1/admin-install.lock` | Conditional state | Installer creates it for install/lock activity; exact empty-file contents alone are hash-authorized. |
| `/var/lib/athena-alpha-m1` | Required protected state root | Installer checks it as a protected prerequisite. |
| `/var/lib/athena-alpha-m1/backups` | Required protected backup root | Installer requires it; individual backup children are conditional. |
| `/root/athena-alpha-m1-stage` | Conditional staging prerequisite | Source staging input, not a durable post-install artifact. |
| `/run/athena-alpha-m1` | Optional volatile runtime state | Runner creates it on demand; only missing leaf after successful parent traversal is optional absence. |
| `/var/log/athena-alpha-m1` | Required log directory | Installer checks and verifies it. |
| `/var/lib/athena-alpha-m1/systemd-canary.json` | Conditional installation precondition | Fresh canary receipt is required for installation, but not universally created by installer. |
| `/var/lib/athena-alpha-m1/ext4-fixture` | Conditional fixture | Created by fixture preparation workflow. |
| `/var/tmp/athena-d67-ext4` | Historical evidence target | Current state cannot prove historical absence; preserve all residuals and evidence. |
| `/opt/athena-qualification/build` | Required installer input | Authenticated build path checked by installer. |

## Verification

Exact commands:

- `bash -n qualification/m30-readonly-acquisition.sh` — PASS.
- `python3 -m py_compile tests/m30_readonly_acquisition_test.py` — PASS.
- `python3 tests/m30_readonly_acquisition_test.py` — 30 PASS, 1 SKIP, 0 FAIL (31 cases). The skipped Unix-domain socket case was blocked by sandbox `EPERM`.
- `git diff --check --no-index /dev/null qualification/m30-readonly-acquisition.sh` — PASS.
- `git diff --check --no-index /dev/null tests/m30_readonly_acquisition_test.py` — PASS.
- `git diff --check --no-index /dev/null docs/development/monad-0.1-alpha-m30-qualification.md` — PASS.

No CTest case applies to this standalone collector. There were no test timeouts. Overall synthetic results: **30 PASS, 1 SKIP, 0 FAIL, 0 TIMEOUT; 0 production tests run.**

Coverage includes root pathname replacement by a symlink, descendant/ancestor symlinks, concurrent disappearance, FIFO, injected mount/device boundaries and unavailable mount identity, low descriptor limit and deep trees, depth/entry/hash/output budgets, directory/file mutation, output replacement, exclusive destination behavior, short/failed writes, file/directory fsync failures, required/optional absence, test-mode allowlist isolation, and unauthorized content remaining unread. Fault-injected mount outcomes are not real mount tests.

## Residual risks and unverified requirements

- The approved Debian VM identity check and actual production acquisition were NOT TESTED.
- Real mount-ID parsing, device/mount boundary transitions, ownership/mode behavior on the target filesystem, and durable storage behavior were NOT TESTED.
- Root-only synthetic refusal was not executed because no root run was authorized or needed.
- Concurrent mutation is detectable only within the non-atomic observation window; no snapshot is provided.
- A crash or storage failure can leave preserved partial files. A failed command is not a durable evidence receipt.
- An independent audit was not performed. This is an engineering self-review of the supplied M29 baseline.

**Acquisition readiness:** NO_GO. **Alpha M1 release readiness:** NO-GO / NOT QUALIFIED. The production VM campaign and evidence review remain unverified, and M30 itself requires operator review before any acquisition authorization.

**Next operator action:** request bounded source review of the M30 collector and tests, then decide separately whether to authorize preparation of the approved Debian VM campaign. This report does not authorize executing the collector.
