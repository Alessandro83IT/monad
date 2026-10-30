# Monad 0.1 Alpha M24 — M20–M24 macroqualification review

**Decision: NO-GO. Alpha M1 remains BLOCKED.** This report reconciles accessible source and local namespace observations with the M14–M19 records. No VM installation, canary, collector, runner, privileged test, recovery, rollback, or protected-state mutation was performed. The requested M22 test was inspected but not run because the required evidence directory is mounted read-only in this environment.

## 1. Executive summary

**VERIFIED BY CODEX:** branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`, remote `origin` at `git@github.com:Alessandro83IT/monad.git` (fetch/push). The worktree was already extensively dirty: 141 tracked/untracked status entries at inspection, including source/test changes, reports, scripts, and existing build/evidence directories. No pre-existing item was changed or removed. This report is the only file created.

**VERIFIED BY CODEX (local namespace only):** the current process is UID 1000 in a `container-other` virtualization environment, with Debian 13 userspace and boot ID `e6e93d12-f3c7-4822-b0dc-406681be3d5b`. This boot ID matches the operator-supplied current ID, but does not establish that this container is the named VirtualBox guest. `VBoxManage` is unavailable. The actual target VM UUID, snapshot, guest state and hypervisor evidence remain **UNKNOWN**.

The installer publishes three files under `/usr/local/sbin` and `/etc/sudoers.d`; the canary is staged separately. Accessible local source hashes equal the installer’s four source pins. The operator-reported historical receipt hashes do not equal current runner/canary hashes and its reported boot ID is historical. The exact receipt-generating source revision is **UNKNOWN**. A local sudoers byte match does not establish installed-file provenance.

The supplied transaction observations remain **OPERATOR OBSERVATION**: zero-byte `install-started`, zero-byte `runner.absent` and `sudoers.absent`, no reported `install-complete` or `rollback-complete`. The first marker payload conflicts with this checkout’s transaction writer. Because the transaction directory, full manifest, current support destination and other protected state cannot be read here, the administrative state is **INDETERMINATE**. The absent markers record original state only; they do not prove current target absence.

M14 historical evidence reports 134/134 filtered nonprivileged tests passing and the complete timeout-sharded fault matrices passing. It separately records a qualification-driver FAIL, 35 tests NOT RUN, 16 present inputs missing from the official inventory, and seven unreconciled binary pins. These are historical results, not results of M24 or Alpha M1 qualification. M15 closure remains a local candidate; M16/M19 already conclude NO-GO. This review resolves no qualification blocker.

## 2. Evidence classification and scope

- **VERIFIED BY CODEX** means directly read or calculated in this checkout or current unprivileged namespace during M20–M24.
- **OPERATOR OBSERVATION** means supplied in the mission or M18/M19 records and not independently acquired from the protected target.
- **DOCUMENTED HISTORICAL CLAIM** means a prior report’s recorded result; it is not a new execution here.
- **INFERENCE** means a conclusion derived from cited code and stated observations.
- **UNKNOWN** means evidence was inaccessible, not supplied, or insufficient. It does not mean absent.

Protected paths were not accessed with privilege escalation. No sudo or administrative command was used. The attempted nonprivileged `stat` reported permission denied for `/root/athena-alpha-m1-stage`, the historical backup transaction, receipt and D6 fixture child. These are **inaccessible**, not absent. `/var/tmp/monad-alpha-tests` exists as owner UID 1000 mode 0700, but creating a new child failed with `Read-only file system`; therefore no test artifacts were redirected elsewhere.

## 3. M20 — forensic evidence reconciliation

### 3.1 Git and environment

| Observation | Classification | Evidence |
|---|---|---|
| Branch `main`; HEAD `abd6bac4b368427b76f6c98522afde928b418939` | VERIFIED BY CODEX | `git branch --show-current`, `git rev-parse HEAD` |
| `origin` fetch and push remote as above | VERIFIED BY CODEX | `git remote -v` |
| 141 status entries; dirty tracked files and extensive untracked user work | VERIFIED BY CODEX | `git status --short --branch`; no cleanup/reset performed |
| Debian GNU/Linux 13 userspace, UID 1000, `container-other`, boot ID `e6e93d12-f3c7-4822-b0dc-406681be3d5b` | VERIFIED BY CODEX, local namespace only | `id`, `/etc/os-release`, `systemd-detect-virt`, `/proc/sys/kernel/random/boot_id` |
| Named VM UUID `e475ea91-72e9-4157-9ee6-583e9ae0462f`, snapshot identity and current guest state | UNKNOWN | VirtualBox tooling unavailable; no hypervisor or guest connection acquired |

The local namespace’s boot ID agrees with the operator-reported current ID. That coincidence is not VM identity proof. Prior M16 also documented that VirtualBox tools were unavailable in its environment; that remains a historical claim, while the current `command -v VBoxManage` returned no path.

### 3.2 Destinations, staged files and hashes

The installer source at `qualification/install-athena-alpha-m1-admin.sh:8-23` defines these locations and pins:

| Artifact | Path / role | Expected metadata and digest | Current evidence |
|---|---|---|---|
| Runner | Source `/root/athena-alpha-m1-stage/athena-alpha-m1-runner.py`; destination `/usr/local/sbin/athena-alpha-m1-runner` | staged source expected root-owned protected stage, mode 0600 per `docs/qualification/alpha-m1-admin-staging.md:49-52`; installed root:root `0755`; SHA-256 `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` | Local source digest VERIFIED BY CODEX. Local namespace reports destination regular file mode 0755, mapped UID:GID `65534:65534`; installed bytes/actual VM owner unknown. |
| Support module | Source `/root/athena-alpha-m1-stage/gateway_support.py`; destination `/usr/local/sbin/gateway_support.py` | staged root-owned mode 0600; installed root:root `0644`; SHA-256 `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` | Local source digest VERIFIED BY CODEX. `stat` returned ENOENT for this path in the current namespace; this does not prove absence on VM. |
| Sudoers | Source `/root/athena-alpha-m1-stage/athena-alpha-m1-sudoers`; destination `/etc/sudoers.d/athena-alpha-m1` | staged root-owned mode 0600; installed root:root `0440`; SHA-256 `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` | Local source digest VERIFIED BY CODEX. Local namespace reports regular file mode 0440, mapped UID:GID `65534:65534`; VM bytes/validity/provenance unknown. |
| Canary | Source/stage `/root/athena-alpha-m1-stage/athena-alpha-m1-systemd-canary.py`; not installed by this installer | staged root-owned mode 0600; SHA-256 `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` | Local source digest VERIFIED BY CODEX; stage path permission denied, contents and metadata UNKNOWN. |
| Receipt | `/var/lib/athena-alpha-m1/systemd-canary.json` | root:root mode 0600; freshness, current boot/systemd, and runner/canary pins checked by installer (`:230-272`) | Historical receipt details are OPERATOR OBSERVATION only; path is inaccessible here. |
| Build inputs | `/opt/athena-qualification/build` | Six binaries plus generated `CTestTestfile.cmake` pinned by runner/installer | No fresh hash or provenance acquisition in M24. M14/M16 record seven local-vs-historical pin mismatches. |

The four local SHA-256 commands returned the exact digests above. These verify checkout bytes and their agreement with current source literals only. They do not authenticate staging, installation, build causality, authorship or custody. Current destination metadata in this namespace uses mapped IDs and cannot be represented as actual VM root ownership.

### 3.3 Evidence inventory and distinctions

| Evidence item | Status and classification |
|---|---|
| Installed runner | Local namespace path is visible as regular mode 0755 with mapped owner; no content hash, ACL, actual guest owner or VM identity. VM installation remains UNKNOWN. |
| Installed support module | Local namespace `stat` says path not found; inaccessible VM state means target presence UNKNOWN. |
| Installed sudoers | Local namespace path visible as regular mode 0440 with mapped owner; operator reports digest matching draft. No guest hash, `visudo -c`, ACL or provenance proof. |
| Historical backup files and transaction directory | OPERATOR OBSERVATION gives selected markers only. Directory inaccessible here; payloads, originals, sidecars, manifest, transaction record, lock, statuses and unreported files UNKNOWN. |
| Transaction completion | Operator says `install-complete` and `rollback-complete` not observed. This is not an independent exhaustive inventory. State remains indeterminate. |
| Canary receipt | OPERATOR OBSERVATION: schema 1, passed true, receipt boot ID `3fec68c2-2d94-45ea-b574-00249796f987`; runner hash `5d6ee35070954d97034de7f0323204995fe0ca86ed12fad774a3acb9ada021d5`; canary hash `5919ef00cda396afb71c0c772fdf5814c98fdcebac30c73dc0f468721502a148`. Raw bytes/time/systemd version/current path metadata inaccessible. |
| Staged artifacts | Stage path permission denied. Exact staged bytes, owner/mode/ACL, inventory and custody UNKNOWN. |
| D6 fixture | Operator says `/var/lib/athena-alpha-m1/ext4-fixture/athena-d6-8yRDjc` contains historical generations/projection/store/lock structures. Child path permission denied here. Preserve; do not treat as empty. |
| D6 parent | Local namespace sees `/var/tmp/athena-d67-ext4` directory mode 0700, mapped UID:GID `65534:65534`; contents inaccessible/unchecked. Operator says root-owned 0700 ext4 and empty. These statements are not independently reconciled. |
| systemd units, active processes, cgroups and mounts | No VM evidence obtained in M24. Local container observations cannot establish guest unit/process/mount state. UNKNOWN. |

### 3.4 Minimal approved read-only acquisition plan

On the identified VM, an administrator should capture one UTC-stamped, immutable raw record tied to VM UUID, approved snapshot, machine ID and boot ID. Before any later action, acquire no-follow type/owner/group/mode/link/ACL/hash metadata for all three installed destinations, the staged artifact directory, receipt, transaction tree, backup manifest and payloads, log directory, fixture, D6 parent/residual and root-owned build. Preserve exact names, marker bytes and errors; do not read D6 payload contents. Capture `findmnt` source/type/options, child mount inventory, systemd unit state and cgroup/process membership using read-only queries. Run `visudo -c` only as a separately authorized read-only validation step. Record any access denial as inaccessible. Preserve original output and hashes; do not repair markers, create fixture, mount, start/stop services, or invoke operational tools.

The M19 runbook’s proposed bounded command inventory and target paths are in `docs/development/monad-0.1-alpha-m19-admin-reconciliation.md:116-130`; use it only after VM identity and read scope are approved. It must be extended to include support destination, every transaction entry, stage metadata, units/processes/cgroups and all mounts, not merely the earlier wrong `/usr/local/libexec` paths.

## 4. M21 — artifact and transaction reconciliation

### 4.1 Transaction state machine and marker semantics

The current installer source reconstructs this order (`qualification/install-athena-alpha-m1-admin.sh:728-858`): acquire lock and reject incomplete prior transactions; allocate a new backup directory; capture originals or absence markers for runner/support/sudoers; write and fsync aggregate backup manifest and transaction record; write `prepared`; create and fsync three sibling temporary files; persist `install-started`; independently rename runner, support, then sudoers with parent-directory fsync; verify hashes, metadata and sudoers syntax; persist `install-complete`.

| Marker | Exact semantics in current source | Meaning of supplied historical observation |
|---|---|---|
| `install-started` | `mark_once` writes `transaction=<backup-directory-name>\n` and fsyncs it (`:688-717`, `:823-825`). It is the publication boundary before the first destination rename. | Zero bytes conflicts with the current implementation. Could indicate a different revision, truncation/interruption, alteration, or observation/reporting error. Which explanation applies is UNKNOWN. |
| `install-complete` | Written only after all three renames and destination validation (`:827-842`). It records installer completion, not independent qualification. | Not observed per operator; missing completion does not show whether any rename occurred. |
| `rollback-complete` | Written after explicit approved rollback and restoration checks (`:845-858`). | Not observed per operator; no conclusion that rollback did or did not partially run without full inventory. |
| `runner.absent`, `support.absent`, `sudoers.absent` | Empty `touch` files record original target absence at backup capture (`:790-803`). Exactly one `.absent` or `.existed` per target is required by manifest verifier (`:397-422`). | Reported runner/sudoers markers are consistent with marker format, but only describe pre-install state. Current destinations may be absent, installed, or changed later. Support marker unknown. |
| `prepared` | Backup set and transaction record prepared before candidate publication (`:807-810`). | Presence/content not supplied; UNKNOWN. |
| `install-partial` | EXIT trap attempts to write transaction ID and exit code after nonzero exit following start (`:672-679`). SIGKILL/power loss may bypass it. | Not supplied. Its absence would not exclude interruption. |
| `rollback-started`, `rollback-partial`, per-file rollback status | Explicit rollback records start and incremental outcomes. Rollback is sequential and can leave mixed state (`:518-568`, `:849-857`). | Not supplied; UNKNOWN. |

Publication is atomic per file, not as a three-file set. The installer does not automatically roll back after crossing `install-started`; it records partial failure best-effort and preserves transaction evidence. The source states no commit point that makes all three renames atomic. The effective install completion point is the durable `install-complete` marker after verification. Missing terminal evidence leaves state indeterminate.

### 4.2 Interruption scenarios

Given a valid but nonterminal start marker, plausible states include interruption after marker and before any rename; after one rename; after two; after all three but before verification/completion; or during an incomplete marker write/fsync. If the zero-byte start marker is accurate, the current code path does not explain it without revision divergence or corruption/alteration. None of these possibilities is a historical finding. Only full raw transaction plus destination evidence can distinguish them.

### 4.3 Receipt, sudoers and pins

| Item | Current checkout / pin (VERIFIED BY CODEX) | Historical receipt or VM claim | Assessment |
|---|---|---|---|
| Runner | `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` | receipt `5d6ee35070954d97034de7f0323204995fe0ca86ed12fad774a3acb9ada021d5` | Discrepancy verified between local source and operator receipt record. The historical artifact/revision match is UNKNOWN. |
| Canary | `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` | receipt `5919ef00cda396afb71c0c772fdf5814c98fdcebac30c73dc0f468721502a148` | Discrepancy verified between local source and operator receipt record. No historical canary file match established. |
| Sudoers | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` | operator reports same digest installed | Byte equality is an operator-reported match; provenance, VM-side validity and custody remain UNKNOWN. |
| Support | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` | no receipt binding | No installed hash supplied. |

The receipt boot ID predates the reported current boot. The installer requires receipt boot ID and systemd version to match current state, receipt age no more than 24 hours, `passed=true`, and exact pinned runner/canary hashes (`:230-272`). Therefore the supplied receipt is not valid for the current boot/candidate by the reported values. It must be preserved. M19 notes an older Session 33 runner digest match, which establishes only a historical local candidate record, not that the receipt came from that source revision. No historical source-revision match is asserted.

The local sudoers draft’s hash exactly matches the installer literal and operator-reported installed digest. The policy is a fixed command allowlist; the draft file is `qualification/athena-alpha-m1-sudoers`. This match does not authenticate VM installation. Installed syntax (`visudo -c`), ACL, owner, actual content and staged provenance remain unverified.

The seven protected build pins remain unreconciled as documented in M15/M16. M15 lists local M14 hashes that differ for `activation_execution_privileged_test`, `activation_plan_retention_test`, `activation_plan_persistence_test`, `legacy_d3_test`, `initial_activation_test`, `libathena_core.so`, and `CTestTestfile.cmake` (`docs/development/monad-0.1-alpha-m15-release-evidence-20261028.md:86-100`). No pin, baseline or official inventory was changed.

## 5. M22 — safe nonprivileged verification

The most directly relevant test is `tests/alpha_m1_installer_transaction_test.py`. Source inspection confirms its `TransactionFixture` creates a private temporary root, redirects installer paths through `MONAD_ALPHA_M1_TEST_ROOT`, requires non-root execution, and exercises install/rollback only inside that fixture (`tests/alpha_m1_installer_transaction_test.py:1-105`; installer `:25-46`, `:758-775`). It does not target host systemd, sudoers, mounts, cgroups or protected administrative destinations in that test mode. It was selected as relevant to marker/interruption semantics.

| Test | Exact command | Start / completion (UTC) | Exit | Result | Evidence |
|---|---|---|---|---|---|
| `alpha_m1_installer_transaction_test.py` | `TMPDIR=/var/tmp/monad-alpha-tests/<new-unique-dir> TMP=<same> TEMP=<same> python3 tests/alpha_m1_installer_transaction_test.py` | Not started; output directory creation failed before test invocation | N/A | **NOT RUN** | No output artifact. `mkdir -m 700 /var/tmp/monad-alpha-tests/m20-m24-20261028-143918-17920` returned `Read-only file system`. |

No test is reported PASS or FAIL for M22. The evidence path must be writable as required before this targeted test is executed. No broad CTest suite or operational harness was run. Historical M14 outcomes remain separate: 134/134 filtered suite PASS and exhaustive sharded persistence/selection fault matrices PASS; qualification driver FAIL; 35 NOT RUN (25 privileged, 5 scanner, 4 static/mock Alpha M1, 1 driver). See `docs/development/monad-0.1-alpha-m14-timeout-reconciliation-20261028.md:25-74` and M16 `:112-121`.

## 6. M23 — recovery readiness (documentation only)

All recovery commands below are **documentation only**. No recovery, install, rollback, fixture preparation or canary was executed. For every case, preserve the historical D6 fixture, receipt, logs, backups and raw evidence; capture no-follow hashes/metadata before any authorized mutation. Any mismatch, missing item or access denial stops automation. State-changing action requires a separately reviewed root-owned hash-pinned artifact, exact argument allowlist, named VM/snapshot, administrator approval and recorded recovery plan.

| Case | Required evidence and preconditions | Proposed recovery and expected postconditions | Failure modes / authorization |
|---|---|---|---|
| A. Installation completed | Valid `install-complete`, transaction identity/manifest, all originals, current three destination hashes/owner/mode/ACL, sudoers validation, no unexplained unit/process/cgroup/mount state. | If installation is intended, retain it; independently verify all three targets equal approved pins and policy passes syntax validation. Preserve backup. | A completion marker alone is insufficient. Any mismatch moves to F/G/I. Administrative approval required for accept/use. |
| B. Interrupted before publication | Full manifest proves backups; `install-started` absent; all current targets match recorded originals; no temp files or unexpected state. | No target rollback needed by source semantics; preserve transaction and have authorized administrator determine whether a new unique transaction is appropriate after all blockers close. | Missing evidence or changes means I. New install requires separate explicit installation authorization. |
| C. Interrupted during publication | Valid transaction identity/manifest, raw markers, all three target hashes and metadata, sibling temporary files, logs and exact ordering evidence. | Do not rerun install. Administrator selects either preserve/complete under a reviewed procedure or approved rollback; verify all three targets afterward. | Per-file publication can yield mixed state; no atomic repair. Explicit administrative approval and exact artifact/arguments required. |
| D. Runner installed, support missing | Current runner/support/sudoers hash/metadata and transaction/backup manifest; runner’s import/runtime dependencies; unit/cgroup state. | Treat as partial install. Preserve. Use reviewed approved repair or restore all recorded originals; verify imports, metadata and all targets. | Runner may fail at runtime or load unexpected module. No direct copy from checkout. Human authorization required. |
| E. Sudoers installed, runner missing | Exact sudoers bytes, syntax, owner/mode/ACL; runner/support state; transaction and backup provenance. | Preserve policy evidence. Authorized administrator may restore recorded original policy or install a fully reviewed coherent artifact set; validate globally and per-file. | Avoid leaving privilege policy unexplained; no manual edit. Human authorization required. |
| F. Markers inconsistent | Raw bytes and metadata of every marker; transaction ID; manifest and backup inventory; target state. | Quarantine operational decision, not files: preserve all and reconcile against installer version/source. Do not alter or synthesize markers. | Zero-byte `install-started` is inconsistent with current code. No automated recovery; independent forensic/admin review and explicit approval required. |
| G. Artifact provenance unknown | Source/stage/destination hashes, authenticated build records, approval/custody, receipt bindings, official inventory and pins. | Do not execute or promote unknown artifacts. Recover only from independently authenticated originals and authorized manifest. | Matching digest alone is not provenance. Security review and human release/admin approval required. |
| H. Rollback required | Exact transaction ID; verified complete backup manifest; prior-state hashes/metadata/ACL; current destination state; explicit approval token bound to transaction and manifest; recovery snapshot. | Documentation command shape only: `/path/to/reviewed/install-athena-alpha-m1-admin.sh rollback /var/lib/athena-alpha-m1/backups/<exact-run> --transaction-id <id> --approval-file <approved-token> --approval-sha256 <digest>`. Then independently verify all targets and preserve backup. | Sequential rollback can partially fail. Stop on any mismatch; no retries or manual deletion. Separate human authorization required immediately before mutation. |
| I. State indeterminate | Any missing/inaccessible/contradictory evidence, including present mission record. | Preserve state and perform only the bounded read-only acquisition in §3.4. No install, canary, rollback or test on VM. | Current state is I. Administrative recovery cannot proceed until evidence resolves it and an authorized decision is recorded. |

### Prerequisites for later actions

- **New canary:** identify approved VM/snapshot/current boot; select and independently review exact runner and canary bytes; establish authenticated staged provenance and writable protected stage; inspect systemd/cgroup behavior and receipt destination/archives/lock; preserve historical receipt; reconcile transaction and D6 state; obtain separate explicit approval for exact canary command and receipt path. Result must bind hashes, current boot/systemd and freshness. A canary does not authorize installation.
- **Administrative install or repair:** close transaction to a known state; complete read-only destination/stage/backup/D6 acquisition; preserve all historical evidence; establish approved empty fixture only through a reviewed design (the reported nested D6 fixture is not reusable); authenticate candidate/build and exact inventory; validate pins, ACLs, paths, sudoers and receipt; use separately approved root-owned pinned installer/gateway and new unique backup path with exact allowlist and recovery plan; obtain separate human authorization.
- **Privileged Alpha M1 qualification:** close source closure and official inventory; resolve all binary pins with authenticated provenance; approve VM/snapshot and candidate; satisfy canary/install gates; verify exact 18-case inventory and capabilities; authorize only named cases through reviewed gateway; retain per-case logs/exit/status and independent evidence review. Any required case not PASS prevents GO. Local tests do not qualify the VM.

## 7. M24 — final qualification gate review

### 7.1 Source closure and inventory

M15’s closure is a **candidate**, not an approved/promoted baseline. M16 reports its local digest and byte checks as consistent for listed inputs, but limits this to local integrity. M14/M15/M16 record 16 existing source/test inputs missing from the official qualification inventory and a qualification-driver FAIL in test setup; the official inventory remains incomplete. Do not silently add those entries or change the baseline. Owner review and explicitly approved versioned inventory promotion, followed by a passing driver, are required. References: `docs/development/monad-0.1-alpha-m15-release-evidence-20261028.md:48-84,116-123`; `docs/development/monad-0.1-alpha-m16-gate-review-20261028.md:15-22,45-68`.

### 7.2 Gate matrix

| Gate | Status | Basis |
|---|---|---|
| Local source hashes versus current installer literals | PASS, limited | Four current source hashes recalculated in M24; does not prove staging/install provenance. |
| VM identity / snapshot / approved environment | BLOCKED | Current environment is a container; UUID/snapshot not acquired. |
| Installed runner/support/sudoers state | UNKNOWN / BLOCKED | Namespace mapping/inaccessibility; no complete guest hashes, ACL or provenance. |
| Transaction state | INDETERMINATE / BLOCKED | Operator marker observations conflict in start-marker size with current code; backup and all destinations inaccessible. |
| Sudoers content and policy validity on VM | BLOCKED | Reported hash matches draft but is unauthenticated; no raw file or `visudo` evidence. |
| Canary current-candidate/current-boot readiness | FAIL for supplied record | Reported historical receipt hashes differ from current pins and boot ID; preserve it. A fresh receipt is not authorized. |
| D6 and fixture preservation/readiness | NOT VERIFIED / BLOCKED | Operator-reported nonempty nested fixture conflicts with empty-fixture prerequisite (`installer.sh:216-228`); metadata not independently acquired. Preserve. |
| M22 targeted test | NOT RUN | Required evidence directory is read-only; no result artifacts. |
| Historical M14 filtered nonprivileged tests | PASS, historical only | 134/134 and timeout-sharded matrices documented; not Alpha M1 qualification. |
| Official qualification inventory | FAIL / BLOCKED | 16 unlisted inputs and historical driver FAIL remain unresolved. |
| Seven protected build pins | BLOCKED | M14 local artifact hashes differ from historical pins; no authenticated originals/promotion. |
| Authenticated source-to-binary provenance / custody | BLOCKED | Local hashes/manifests are not signed or independently held; causal build proof absent. |
| Systemd, processes, cgroups, mounts | UNKNOWN | No current guest acquisition. |
| Required privileged campaign and scanner evidence | NOT RUN / BLOCKED | No campaign or scanner run authorized or executed. |
| Independent evidence review and release approval | BLOCKED | No completed campaign dossier or reviewer acceptance. |

### 7.3 Technical, administrative and release blockers

**Technical:** official inventory/driver failure; seven binary pin discrepancies; unapproved source closure; no authenticated build provenance; fresh canary must match reviewed candidate; no independently reviewed current guest systemd/cgroup/mount behavior; M22 targeted transaction test not run due read-only evidence path.

**Administrative:** transaction remains indeterminate; full backup and destination evidence unavailable; support state unknown; sudoers syntax/provenance unknown; stage inaccessible; D6/fixture state unverified and reported nonempty; VM UUID/snapshot not independently established; no separate install, rollback, canary or campaign authorization.

**Release engineering:** no approved inventory promotion, candidate freeze, signed custody/provenance, reconciliation/promotion of pins, complete required privileged and scanner evidence, independent evidence review, or release authorization. Closing administrative installation alone would not close these gates.

## 8. Exact next authorized action

**Perform a bounded, read-only acquisition on the named Debian VM after an administrator identifies and records the VM UUID/snapshot and approves the exact read scope.** Capture raw no-follow metadata and hashes for all actual destinations, stage, receipt, complete transaction/backup tree, D6 and fixture metadata, build artifacts, units/processes/cgroups and mounts, tied to machine ID and current boot ID. Preserve every existing item and record access denials as inaccessible. Do not invoke canary, installer, runner, collector, test harness, rollback or recovery. Review this acquisition independently before any later state-changing proposal.

## 9. Supporting references

- `qualification/install-athena-alpha-m1-admin.sh:8-23,216-272,397-467,672-754,728-858` — destinations, pins, fixture and receipt constraints, backup/marker semantics, publication and rollback.
- `qualification/athena-alpha-m1-sudoers` — current sudoers draft; its current hash was calculated during M24.
- `qualification/athena-alpha-m1-systemd-canary.py` — canary receipt binding/publication implementation; not run.
- `tests/alpha_m1_installer_transaction_test.py:1-105` — rootless transaction test fixture inspected but not run.
- `docs/qualification/alpha-m1-admin-staging.md:49-75,89-105,119-164` — stage metadata, workflow, recovery limits and scoped read acquisition.
- `docs/qualification/alpha-m1-recovery.md:3-31` — preservation, backup and separate activation gates.
- `docs/development/monad-0.1-alpha-m17-d6-reconciliation.md:23-79` — M17 transaction/D6/receipt analysis; historical/operator claims distinguished.
- `docs/development/monad-0.1-alpha-m19-admin-reconciliation.md:19-145` — M19 destination correction, marker semantics, receipt/pins and decision tree.
- `docs/development/monad-0.1-alpha-m14-timeout-reconciliation-20261028.md:25-74,76-116` — historical test outcomes and official inventory issue.
- `docs/development/monad-0.1-alpha-m15-release-evidence-20261028.md:48-100,102-132` — candidate closure, missing inputs, pins and release blockers.
- `docs/development/monad-0.1-alpha-m16-gate-review-20261028.md:11-29,31-43,70-129` — gate review, pin divergences, historical test counts and NO-GO.

## 10. Final decision

**NO-GO. Alpha M1 remains BLOCKED.** M20 reconciled accessible facts and explicit unknowns; M21 reconstructed code semantics and identified the marker discrepancy; M22 established a safe test candidate but could not run it in the mandated evidence location; M23 prepared a recovery decision tree without recovery; M24 consolidated the gate review. No qualification blocker was resolved, no protected state changed, and no privileged result is claimed.
