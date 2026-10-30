# Monad 0.1 Alpha M29 security closure and acquisition readiness

## Executive summary

M29 is a separate, reviewable candidate derived from M28.3. It corrects confirmed findings in the production VM identity gate and exception reporting, adds Python isolated mode, and extends synthetic output fault coverage. Its tests pass synthetically, but no production VM checks or real mount tests were run. There are residual high-severity/qualification blockers in coverage and a known path-based synthetic-root race. Decision: **NO_GO** for privileged acquisition. Alpha M1 remains **NO-GO / NOT QUALIFIED**.

No administrative action was performed. No sudo, installer, runner, mount, privileged test, network, or historical-evidence operation was run. Existing M28.3 and the pre-existing dirty worktree were preserved.

## Source baseline

- Repository: `/home/alessandro/Projects/monad`
- Branch: `main`; baseline `HEAD`: `abd6bac4b368427b76f6c98522afde928b418939`
- Worktree was already heavily modified and contained untracked qualification evidence/build directories before this task. Those were left untouched.
- Baseline files inspected: `qualification/m28-3-readonly-acquisition.sh`, `tests/m28_3_readonly_acquisition_test.py`, and `docs/development/monad-0.1-alpha-m28-3-qualification.md`.
- Contract sources inspected: `qualification/install-athena-alpha-m1-admin.sh`, `qualification/athena-alpha-m1-runner.py`, `qualification/alpha-m1-verified-evidence-runner.md`, and `docs/development/monad-0.1-alpha-m28-acquisition-plan.md`.

## Severity-ranked findings

| Severity | Finding and affected code | Scenario / impact | Remediation and regression coverage |
|---|---|---|---|
| HIGH | Production product UUID in M28.3 identity gate was only required to be nonempty. | Collector could accept a different VM with matching hostname, machine ID and serial but an unrelated product UUID. | M29 requires exact preflight UUID `91ea75e4-e972-5741-9ee6-583e9ae0462f`. Static source check only; production identity path was not run. |
| HIGH | Unexpected Python exceptions in the M28.3 expected-path loop could escape before report creation. | A fault or programming exception after traversal start could terminate without the promised `PARTIAL` inventory. | M29 catches ordinary exceptions at each top-level allowlisted path, emits `TRAVERSAL_EXCEPTION`, and preserves `PARTIAL`; failures during final output still return 2 with created files preserved. Not all exception classes are fault-injected. |
| HIGH | Synthetic root validation used `islink` followed by path-based `stat`; later traversal re-opened it by pathname. | A concurrent replacement can change the root between validation and use. This is restricted to synthetic mode, but undermines fixture isolation and can expose paths visible to the test process. | M29 retains this inherited behavior; regression needed is fd-anchored root validation and traversal. This confirmed gap remains a release blocker. |
| MEDIUM | M28.3's comment and report described every fixed path as an installer post-install requirement. | Stage, build inputs, backup root, canary receipt, lock, and fixture have precondition/conditional roles and may not exist in every historical state. | M29 report classifies each allowlisted path from source evidence; absence is never interpreted as proof a historical path never existed. No behavior was weakened pending approved policy change. |
| MEDIUM | Test suite lacked deterministic file-fsync and directory-fsync failure tests and short-write assertion. | Storage errors could leave incomplete output; checksum correctness after short writes was not proven. | M29 adds injected file/directory fsync failures and short-write completion/checksum tests. |
| MEDIUM | Production mount-ID, real device crossing, output ancestry race, and effective-root synthetic refusal have no safe dynamic production test here. | Synthetic injected outcomes cannot establish Linux mount namespace or privileged-host behavior. | No privileged tests performed. These remain explicit NOT TESTED and block acquisition. |
| LOW | Descriptor close errors and fd exhaustion were not directly injected. | Resource-pressure or unusual close failures may complicate issue classification. Most opened descriptors are protected by `finally`; exhausted opens are generally classified as local errors. | M29 exception containment improves report behavior, but targeted close/fd-exhaustion tests remain NOT TESTED. |

## Corrective changes and security invariants

M29 is additive. M28.3 and prior artifacts are preserved. Changes in `qualification/m29-readonly-acquisition.sh`:

- Invoke Python with `-I`, preventing `PYTHONPATH`, user site, and related `PYTHON*` environment configuration from changing the interpreter environment.
- Require the exact product UUID observed in the documented operator preflight.
- Convert ordinary unexpected exceptions at an acquisition root into an explicit `TRAVERSAL_EXCEPTION` and partial completion.
- Add short-write injection and file/directory fsync failure injection.

The conservative model remains: fixed allowlist; no content reads except the exact empty installer lock authorization; descriptor-relative traversal; no symlink following; no opening special files; mount ID and device must match; unavailable mount ID stops that tree; bounded depth, entries and bytes; secure new output directory and exclusive files; checksummed inventory; output failures preserve whatever was created. A COMPLETE report only occurs with an empty issue list.

## Administrative path contract matrix

Classifications below distinguish install-time prerequisites, installation postconditions, runtime state, and campaign evidence. The acquisition allowlist is a forensic scope, not itself proof that installation created or preserved an item.

| Path | Classification | Source basis and interpretation |
|---|---|---|
| `/usr/local/sbin/athena-alpha-m1-runner` | REQUIRED | Installer `verify_installed()` checks installed runner type, owner/mode and pin after installation. |
| `/usr/local/sbin/gateway_support.py` | REQUIRED | Same installer postcondition for support file. |
| `/etc/sudoers.d/athena-alpha-m1` | REQUIRED | Same installer postcondition, including `visudo` validation. |
| `/var/lib/athena-alpha-m1/admin-install.lock` | CONDITIONAL | Installer creates it when an install/lock operation runs; not evidence every prior state had an install. Empty-file rule permits only the known exact content hash policy. |
| `/var/lib/athena-alpha-m1` | REQUIRED | Installer checks protected state directory as prerequisite; session state is expected for the campaign. |
| `/var/lib/athena-alpha-m1/backups` | REQUIRED | Installer requires safe backup root before transaction. Individual backup children are conditional. |
| `/root/athena-alpha-m1-stage` | CONDITIONAL | Installer source staging prerequisite. It is not established as a durable post-install artifact. |
| `/run/athena-alpha-m1` | OPTIONAL | Runner creates runtime locks/staging on demand; `/run` state is volatile. Missing leaf is optional only if parent traversal succeeds. |
| `/var/log/athena-alpha-m1` | REQUIRED | Installer requires and verifies the log directory. |
| `/var/lib/athena-alpha-m1/systemd-canary.json` | CONDITIONAL | Installer requires a valid fresh canary receipt as an installation precondition; not universally created by installer. |
| `/var/lib/athena-alpha-m1/ext4-fixture` | CONDITIONAL | Installer's `prepare-fixture` creates it and install verifies it; only exists after that workflow. |
| `/var/tmp/athena-d67-ext4` | HISTORICAL_EVIDENCE_TARGET | M28 campaign and evidence collector refer to D6. Current absence cannot establish historical absence. Preserve the residual and all historical evidence. |
| `/opt/athena-qualification/build` | REQUIRED | Installer checks this authenticated build path and pinned artifacts before installation; requirement applies to that installer campaign, not proof of historical presence. |

No allowlisted path could be called **UNVERIFIED** for its general role because source establishes the classifications above; exact provenance, historical existence and current contents remain UNVERIFIED absent approved VM evidence.

## Threat model and remaining limitations

The collector treats filesystem names, metadata and directory contents as hostile and mutable. It defends against ordinary symlink traversal, special-file reads, device/mount crossings, concurrent entry replacement detectable through identity checks, bounded-tree exhaustion, unauthorized content hashing, and output destination collisions. It does not provide a snapshot/atomic view, authenticate the kernel or evidence origin, prove historical absence, or resist a privileged adversary able to mutate the host namespace. Linux mount behavior must be confirmed on the approved VM. The synthetic root's path validation TOCTOU remains a high-severity fixture-isolation defect and must be corrected before any acquisition campaign.

The output parent must be pre-existing, safe and outside the repository and acquisition roots. The newly created evidence directory and files are mode-restricted. Failure after creation intentionally preserves partial evidence; an output filesystem error cannot guarantee a complete report.

## Test-to-requirement matrix

| Requirement | M29 coverage/result |
|---|---|
| Root/ancestor and descendant symlink handling | Synthetic PASS: root output ancestry, acquisition ancestor and descendant symlink cases. A concurrent replacement specifically at the synthetic root remains NOT TESTED. |
| Concurrent entry replacement/disappearance | Synthetic PASS: disappearance and opened identity mismatch injection. |
| Directory/file mutation | Synthetic PASS: injected file and directory mutation. |
| Mount identity unavailable and boundary changes | Synthetic PASS by injection only; real mount/device transition NOT TESTED. |
| Permission failures, `scandir` errors, read errors | Hash read error is injected PASS; general permission/enumeration errors NOT TESTED. |
| FD exhaustion and descriptor-close failures | NOT TESTED. |
| Resource limits | Synthetic PASS: entry, depth, hash-byte and output limits. |
| Output creation races and unsafe ancestry | Collision, symlink ancestry and writable-parent rejection PASS; adversarial concurrent rename race NOT TESTED. |
| Short writes, write errors, file and directory fsync failures | Synthetic PASS. |
| Checksum correctness | Synthetic PASS, including short-write path. |
| Root-only synthetic refusal | NOT TESTED; no root execution was attempted. |
| Test environment injection isolation | `-I` is present; unauthorized content read assertion PASS. Exact hostile `PYTHONPATH` import probe NOT TESTED. |
| Required/optional path behavior | Synthetic PASS for missing required, optional leaf, and inaccessible/symlinked optional ancestor. |
| Production identity | Exact UUID source gate updated; PRODUCTION VERIFIED: no. |

## Exact verification and totals

Commands run:

- `bash -n qualification/m29-readonly-acquisition.sh` — PASS.
- `python3 -m py_compile tests/m29_readonly_acquisition_test.py` — PASS.
- `python3 tests/m29_readonly_acquisition_test.py` — 27 PASS, 1 SKIP, 0 FAIL; socket test skipped because sandbox AF_UNIX bind returned `EPERM`.
- `python3 tests/m28_3_readonly_acquisition_test.py` — 20 PASS, 1 SKIP, 0 FAIL; same sandbox socket restriction.
- `git diff --check` — PASS for tracked files.
- `git diff --check --no-index /dev/null qualification/m29-readonly-acquisition.sh` — PASS (exit 1 is expected for a difference; no whitespace diagnostic).
- `git diff --check --no-index /dev/null tests/m29_readonly_acquisition_test.py` — PASS (same expected difference status).
- `git diff --check --no-index /dev/null docs/development/monad-0.1-alpha-m29-security-closure.md` — PASS (same expected difference status).
- `rg -n '[[:blank:]]+$' qualification/m29-readonly-acquisition.sh tests/m29_readonly_acquisition_test.py docs/development/monad-0.1-alpha-m29-security-closure.md` — no matches (PASS).
- `command -v shellcheck` — no executable found (SKIP).

Totals for executed unittest cases: **47 PASS, 2 SKIP, 0 FAIL, 49 total**. Additional static checks: 7 PASS and 1 SKIP (ShellCheck unavailable). Requirement-level NOT TESTED: production VM identity/acquisition, root refusal, real mount/device transition, synthetic-root replacement, fd exhaustion/close error, general enumeration/permission failure, and concurrent output replacement. No CTest suite applies to this standalone acquisition candidate.

## Production status, release decision, next action

- **SYNTHETICALLY VERIFIED:** listed tests only.
- **PRODUCTION VERIFIED:** none.
- **NOT TESTED:** requirements listed above; no privileged or VM action occurred.
- **Privileged acquisition readiness:** **NO_GO**. The synthetic-root TOCTOU is a confirmed high finding, and production behavior is unverified.
- **Alpha M1 release readiness:** **NO-GO / NOT QUALIFIED**. Historical evidence and separate qualification campaign requirements remain unsatisfied.
- **Decision:** **NO_GO**.

Exact next operator action: request a bounded, unprivileged code review of the M29 synthetic-root anchoring defect and require its correction plus regression tests before preparing any acquisition authorization. No collector execution is authorized by this report.
