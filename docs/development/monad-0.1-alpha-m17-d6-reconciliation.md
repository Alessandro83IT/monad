# Monad 0.1 Alpha M17 — D6 forensic reconciliation and qualification readiness

**Decision: NO-GO. Alpha M1 remains BLOCKED; no canary, installation, privileged campaign, collector, rollback, cleanup, or D6 access was performed in this session.** The operator-reported administrative audit is recorded as supplied evidence, not as an independent observation by Codex. The residual D6 tree and all administrative evidence must remain preserved.

## 1. Executive summary

The supplied audit reports `/var/tmp/athena-d67-ext4` as an empty root-owned `0700` ext4 directory and reports the residual fixture under `/var/lib/athena-alpha-m1/ext4-fixture/athena-d6-8yRDjc`, containing the known generation/projection/store entries and lock, with no child mounts. It also reports an install backup transaction with `install-started` but no terminal marker, plus `runner.absent` and `sudoers.absent`, and a passed canary receipt. These are **operator-reported observations**; this session did not read those protected paths or independently verify the audit.

The transaction source confirms that `install-started` is the durable boundary before three separate destination renames. Its presence without `install-complete` or `rollback-complete` means the transaction is indeterminate; the destinations may be old, new, or mixed. The reported zero-byte absent markers are consistent with this installer’s marker payload convention for original target absence, but do not establish which destinations currently exist or their content. Do not retry, repair markers, or rollback without a separate recorded administrative decision and full read-only reconciliation.

The supplied canary receipt has `canary_sha256=5919ef…`, `runner_sha256=5d6ee3…`, boot ID `3fec68c2-…`, and systemd 257. The current candidate source hashes are respectively `9d9d4d…` and `c68bca…`; those are also the installer’s fixed values. Therefore the receipt does not bind to this current candidate. The exact historical artifact that generated the supplied receipt is not present in accessible local evidence, so the divergence’s source artifact cannot be identified here. The receipt cannot authorize this candidate’s install.

The D6 residual is **not reusable as the runner fixture**: the runner and installer require `/var/lib/athena-alpha-m1/ext4-fixture` itself to be an empty root-owned mode-0700 ext4 directory. The supplied audit says that directory contains a D6 subtree. Treat the entire fixture and D6 tree as preservation evidence; do not empty, relocate, rename, or mount over it. Any new fixture location/layout requires reviewed source and operational change, plus explicit approval.

Historical records already describe inaccessible D6/fixture paths, incomplete evidence custody, 34 blocked controls and 7 not run (Session 37 historical capture), untrusted build provenance, and no completed privileged campaign. The supplied audit adds useful metadata but does not close these gates. Qualification remains blocked.

## 2. Git review and scope

At session start the repository was on branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`, matching the supplied context. The working tree was already substantially dirty: tracked changes across CMake/source/tests and many untracked reports, scripts, qualification artifacts, evidence and build directories. They were preserved. No reset, clean, commit, push, test, build, collector, runner, canary or privileged command was run.

This report is the only file created by this session: `docs/development/monad-0.1-alpha-m17-d6-reconciliation.md`. Source review used read-only listing, `sed`/`nl`, `rg`, `git status`, `git rev-parse`, and `sha256sum` on repository candidate files. No protected path was read. The operator’s audit is explicitly distinguished from local verification below.

## 3. Installation transaction reconstruction

The relevant implementation is `qualification/install-athena-alpha-m1-admin.sh`:

1. **Preflight:** validates fixed source hashes, protected destination path components, build artifact pins, canary receipt freshness/boot/systemd/hash binding, fixture emptiness/ext4, and backup/log paths (lines 291–320, 216–228, 230–272).
2. **Serialization and transaction allocation:** obtains a persistent `admin-install.lock` with `flock`, refuses incomplete prior transactions, creates a new unique backup directory, and captures each existing destination or its absence (lines 713–739, 777–806).
3. **Prepared evidence:** writes and fsyncs the aggregate backup manifest, transaction identity record and `prepared` marker (lines 360–476, 572–609, 807–810). Backup inventory expects exactly one `{runner,support,sudoers}.existed` or `.absent` marker per target; these are regular mode-0600 files. The payload of these existence markers is not used as status text.
4. **Candidate preparation:** creates three temporary files in their respective destination directories, installs candidate bytes/modes, fsyncs file contents, and validates staged sudoers before publication (lines 811–822).
5. **Publication boundary:** writes and fsyncs `install-started` with `transaction=<id>` (lines 688–693, 823–826). After this point the EXIT handler never restores targets. A nonzero exit attempts to add `install-partial` with transaction ID and exit code (lines 672–685).
6. **Three independent replacements:** runner, support, then sudoers are renamed individually, each followed by parent-directory fsync (lines 827–838). They are not one atomic transaction.
7. **Completion:** verifies destination hashes/metadata and sudoers syntax, then writes `install-complete` (lines 741–754, 839–844).
8. **Explicit rollback:** requires an exact transaction ID and a separately approved token bound to backup and transaction manifest hashes, validates all destination states before mutation, writes `rollback-started`, restores runner/support/sudoers sequentially, records per-file statuses, and finally writes `rollback-complete` (lines 492–569, 611–669, 845–859). A failed/partial rollback is not automatically retried.

Crash/power-loss states after `install-started` include: no rename yet; runner only; runner and support; all three renamed but verification/completion marker absent; a nonzero-exit path with `install-partial`; or a shutdown during marker publication/fsync leaving evidence that must itself be checked. `install-complete` missing is not proof that no destination changed. With no terminal rollback marker, state remains indeterminate even if destination hashes happen to look uniform.

### Rollback and recovery conditions

No automatic rollback occurs after `install-started` in this source version. Ordinary nonzero exit attempts to preserve state and record `install-partial`; SIGKILL and power loss may prevent that marker. The installer refuses new installs while a transaction is incomplete. The runbook requires re-identifying VM/snapshot/boot, preserving all artifacts, recording the three destination hashes and metadata, validating the full backup/transaction manifest, and obtaining a distinct approval before explicit rollback or manual recovery. Any mismatch, inaccessible item, unknown marker, or mixed/unexpected destination blocks automated recovery. Rollback completion does not resume or qualify the campaign.

## 4. Marker analysis

Installer transaction markers (all transaction markers except `install-partial` contain `transaction=<id>\n`):

| Marker | Meaning / implication |
|---|---|
| `prepared` | Backup and candidate preparation reported complete; publication not yet begun if `install-started` absent. |
| `install-started` | Durable publication boundary crossed; targets may be mixed. Nonterminal by itself. |
| `install-partial` | Installer observed nonzero exit after start and recorded exit code, if it could persist marker. |
| `install-complete` | Installer reported all publication/validation finished; independent destination verification still required. |
| `rollback-started` | Explicit rollback began; absence of terminal rollback marker is indeterminate. One-use gate blocks automatic retry. |
| `rollback-partial` | Rollback did not complete; manual recovery review required. |
| `rollback-complete` | Rollback reported complete; independently verify destinations and preserve backup. |
| `runner.existed` / `runner.absent`, `support.*`, `sudoers.*` | Original existence state captured by backup; exactly one per target. Empty payload is consistent with source behavior. These are **not terminal transaction markers**. |
| `rollback-{runner,support,sudoers}.status` | Per-target rollback status detail, potentially partial. |

The operator says the transaction has zero-byte `install-started`, `runner.absent` and `sudoers.absent`. In the current source `install-started` carries a transaction line, whereas `runner.absent`/`sudoers.absent` are created with `touch` and are zero bytes. Thus the reported zero-byte `install-started` is inconsistent with this source’s expected payload and is a stop condition pending raw metadata/content reconciliation. Plausible explanations include another installer revision, truncation/interrupted external modification, or inaccurate transfer of the observation; none is proven. The source also expects `support.existed` or `support.absent`, `prepared`, `transaction.json`, and `backup-manifest.json`; their existence/content was not supplied and remains unknown. Do not “fix” any marker.

Distinct markers outside the transaction directory include the canary receipt `systemd-canary.json` (success receipt, not install terminal state), runner/sudoers destination presence, and transient canary/runner runtime unit and lock state. `install-started`, `runner.absent`, and `sudoers.absent` must not be interpreted as proof that runner/sudoers are currently absent.

## 5. Canary pin analysis

### Accessible source and pin values

Read-only SHA-256 in this checkout:

| Item | Current source / pin | Supplied receipt | Assessment |
|---|---|---|---|
| Runner | `c68bca67a7a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` (installer lines 20–21 and current runner bytes) | `5d6ee35070954d97034de7f0323204995fe0ca86ed12fad774a3acb9ada021d5` | Mismatch; receipt is not bound to current runner. |
| Canary | `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` (installer line 23 and current canary bytes) | `5919ef00cda396afb71c0c772fdf5814c98fdcebac30c73dc0f468721502a148` | Mismatch; receipt is not bound to current canary. |
| Sudoers draft | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` | Not carried in receipt | Current source matches installer literal; no independent staging/installed proof. |
| Protected build artifacts | Six binary pins in runner lines 39–46; installer also pins `CTestTestfile.cmake` (lines 312–320) | Not demonstrated by supplied receipt | Still require authenticated build and exact inventory reconciliation. |

The supplied receipt’s values match neither current source nor installer pins. The older Session 33 report records the runner digest `5d6ee…`; this establishes that those bytes appeared in an earlier local candidate record, not that they generated this receipt or were installed on the VM. No accessible local file containing canary hash `5919…` was found by targeted text search. The protected staging path `/root/athena-alpha-m1-stage` was not read; availability/permissions were not tested in this session. Exact origin of the receipt’s canary hash is therefore **unknown**; assigning it to an old canary revision would be speculation.

Session 35/34B reports historical candidate identities consistent with current runner/canary/installer pins. That historical local review is not authenticated VM staging or receipt provenance. The installer checks receipt hashes, boot ID, systemd version and freshness (lines 230–272), so a fresh canary receipt would be required for the selected exact candidate. This session did **not** execute it.

### Approvals needed

1. Independent review to select/freeze the exact candidate and reconcile receipt-versus-source lineage; do not edit any pin now.
2. Approved VM/snapshot/boot identity and protected artifact/source provenance.
3. Separate explicit approval for read-only acquisition of the existing receipt, staging/destination hashes and metadata.
4. Separate approval naming the exact canary bytes/hash, VM/snapshot/boot and receipt destination for future canary execution.
5. Separate approval for installation and later privileged campaign. Canary success alone authorizes neither.
6. Any pin update requires the project’s explicitly reviewed promotion workflow, with security review and synchronized manifest/baseline/inventory as applicable. No promotion is authorized or performed here.

## 6. D6 qualification matrix

| Requirement | Evidence available | Classification / decision |
|---|---|---|
| Preserve D6 residual and historic evidence in place | Operator reports residual layout and empty `/var/tmp` parent; runbooks repeatedly require no mutation. | Layout is operator-reported; preservation directive is verified in docs. Preserve all. |
| No-follow metadata for parent/residual, owner/mode/ACL, mount/filesystem, child mounts | Supplied audit reports root:root 0700/ext4 and no child mounts, but raw commands/outputs, ACL details and chain of custody are not included. | **Insufficient** as independently reviewable qualification evidence; not a Codex observation. |
| Required names/shape: `generations`, `projection`, `store`, lock path | Operator reports these names and no others to depth 8. Historical Session 37 reports prior EACCES to parent/residual. | Current report is operator-supplied; historical evidence is contradictory in access/result, not necessarily state. Fresh approved metadata-only reconciliation required. |
| Canonical test fixture emptiness | Audit says `ext4-fixture/athena-d6-8yRDjc` exists. Runner’s exact `EXT4_PARENT` is `/var/lib/athena-alpha-m1/ext4-fixture` and it rejects any child (runner lines 24–26, 149–170); installer likewise insists empty (installer lines 216–228). | **Contradictory to readiness requirement**. Not reusable for the current runner. Preserve; never empty or repurpose. |
| `/var/tmp/athena-d67-ext4` empty ext4 parent | Operator-reported only. Historical D6 name is beneath this path, but audit says parent empty; runbooks say residual path is `/var/tmp/athena-d67-ext4/athena-d6-8yRDjc`. | Internal discrepancy with historical expected location/identity. Could reflect separate D6 locations or audit scope; cannot reconcile without raw path inventory and identity chain. Preserve both paths. |
| Receipt, backup, transaction and markers reconciled | Operator provides receipt JSON and a few marker states; not full backup inventory, transaction JSON, manifest, marker payloads, destination hashes, ACLs, or archived receipts. | **Insufficient**; `install-started` zero-byte payload contradicts current source. |
| Historical D6 evidence and custody | Session 37 records 34 BLOCKED controls and 7 NOT RUN in historical 150-control capture; Session 37 report says D6 and protected state inaccessible, not newly acquired. M16 keeps D6 NOT VERIFIED. | Historical evidence is preserved but **not qualification evidence of current VM state**. |
| No mounts/processes/systemd residuals relevant to candidate | Operator reports no child mounts for fixture; no current unit/process/cgroup/log evidence is included. | **Absent from supplied evidence**; old failed-unit observations are not proof of no descendants. |
| Fixture reuse decision | Runner/installer require exact fixture root empty and writable for tests; audit reports data nested within it. | **Do not reuse**. It is protected state to preserve. A future isolated empty fixture needs separate approved design/path and candidate review. |

Classification terms: **absent** = not supplied in the accessible dossier; **not accessible** = prior reports record access denied or this session did not access protected location; **insufficient** = evidence exists but lacks provenance/coverage; **contradictory** = conflicts with current source/runbook or prior expected path. These categories are not PASS/FAIL substitutes.

## 7. Evidence gaps

1. Independent raw capture of current VM UUID/image/snapshot, machine ID, boot ID and time tied to host/hypervisor evidence.
2. Full no-follow inventory and metadata for D6 parent, residual, canonical fixture, `/var/lib/athena-alpha-m1`, backups, logdir, stage, installed runner/support/sudoers/canary and every transaction item. Include ACL, mount and filesystem identity, link count, exact marker contents and child-mount checks.
3. Explain whether D6 at `/var/tmp/...` and `ext4-fixture/athena-d6-8yRDjc` are separate artifacts; establish identity without reading protected payloads or changing names.
4. Complete transaction evidence: all expected files, `backup-manifest.json`, `transaction.json`, terminal/intermediate markers, per-target backup original hashes/metadata, and current three destinations. Validate without mutation. Supplied zero-byte `install-started` conflicts with source.
5. Receipt raw file bytes/metadata and archive history, then identify exact runner/canary bytes that generated it. Current receipt does not match current pins; do not overwrite or archive it.
6. Provenance/authentication of protected build, all seven artifact pins and exact 18-case generated CTest inventory. M16 documents unresolved binary divergences in local builds and official inventory omissions.
7. VM process, systemd unit, cgroup, logs and mount state; prior Session 33 `MainPID=0`/empty ControlGroup did not prove no descendants.
8. Chain of custody for historical 149 files / evidence directory named by Session 39 runbook and acceptance by independent reviewer.
9. Separate human approvals for each future read scope, staging, canary, install, privileged cases, rollback/recovery and any pin promotion.

## 8. Operational plan

All commands below are **documentation only**. None was executed in M17. Execute only after explicit, scoped approval on the identified Debian qualification VM; use a fresh protected evidence output directory and preserve raw stdout/stderr/status.

### Minimum safe order

1. Obtain independent operator/admin review of this operator-supplied audit and authorize a bounded read-only acquisition. Keep D6 and the nonempty fixture untouched.
2. Re-identify VM and snapshot from approved hypervisor interface; bind guest machine/boot/systemd identity. Stop if identity is uncertain.
3. Read transaction directory, exact marker payloads, manifests and current destination metadata/hashes without following links. Reconcile all original/candidate generations. Do not invoke installer rollback or new install.
4. Acquire D6 parent/residual and fixture metadata sequentially (no-follow path walk, names only, no payload), ACL, mount/filesystem, child mounts, and exact paths. Resolve location discrepancy in evidence records, not by moving files.
5. Reconcile receipt and candidate hashes. Keep receipt immutable. If a new canary is later authorized, bind it to a separately selected and reviewed candidate.
6. Close source/build provenance, official inventory and pin promotion review before any campaign decision. No pin regeneration as an investigative step.
7. Perform any nonprivileged static/source review and exact inventory analysis in an isolated local workspace; no test suite was requested or run here. A fresh empty test fixture and VM runtime behavior require separate approvals and cannot be inferred from static review.
8. Only after all read-only evidence review passes: separate approvals for staging; canary; install; campaign; and, if necessary, rollback. For installation require empty new backup path and no unresolved transaction. For recovery, treat existing transaction as incident; no rerun/retry and no state-changing rollback until exact approval is recorded.
9. Independent reviewer evaluates every required case and preservation evidence. GO requires every mandatory case PASS, no SKIP/FAIL/TIMEOUT/NOT RUN, complete evidence and accepted provenance. Release/install gate remains separate.

### Authorization boundary

| Operation | Authorization class |
|---|---|
| Local source/document/hash inspection | Nonprivileged and read-only; allowed (only limited static review executed here). |
| VM identity/OS, transaction metadata, D6/fixture metadata, ACL/mount/unit/cgroup reads | Requires approved VM access and explicit bounded administrative read scope when protected paths need root. Collector invocation itself requires separate approval and was not run. |
| Local isolated source/build reconstruction and nonprivileged tests | Nonprivileged but needs an approved candidate/scope; tests were not run in this session. |
| Transfer/stage files or write evidence to protected VM location | Separate staging/write authorization. |
| Canary execution and receipt publication | Separate privileged execution authorization tied to exact hashes and VM. |
| Installer, sudoers/runner administrative changes | Human administrative approval for exact artifact hashes, paths, VM and backup. |
| Rollback/manual restoration, fixture cleanup/replacement, mount/snapshot/systemd/cgroup changes | Explicit state-changing administrative approval; never implicit or part of this report. |
| Privileged CTest campaign / collector running as admin | Separate campaign/read scope approvals; workspace binaries must not run as root. |
| Pin/baseline/inventory promotion | Explicit reviewed promotion workflow; not authorized here. |

### Future command shapes (not executed)

These examples are placeholders and must be reconciled against the currently approved runbook and exact VM paths before use. They are not ready-to-run authorization:

```sh
# Approved VM identity/preflight capture only, as ordinary user where possible
id
uname -a
cat /etc/os-release
cat /proc/sys/kernel/random/boot_id
systemctl --version
findmnt -T /var/lib/athena-alpha-m1/ext4-fixture -o TARGET,SOURCE,FSTYPE,OPTIONS
findmnt -T /var/tmp/athena-d67-ext4 -o TARGET,SOURCE,FSTYPE,OPTIONS

# Separately approved no-follow metadata-only review; do not read payloads or mutate
namei -l -- /var/lib/athena-alpha-m1/ext4-fixture /var/tmp/athena-d67-ext4 /var/tmp/athena-d67-ext4/athena-d6-8yRDjc
stat -c 'type=%F mode=%a uid=%u gid=%g links=%h path=%n' -- /var/lib/athena-alpha-m1/ext4-fixture /var/tmp/athena-d67-ext4 /var/tmp/athena-d67-ext4/athena-d6-8yRDjc
getfacl -cp -- /var/lib/athena-alpha-m1/ext4-fixture /var/tmp/athena-d67-ext4 /var/tmp/athena-d67-ext4/athena-d6-8yRDjc
find -P /var/lib/athena-alpha-m1/ext4-fixture -xdev -mindepth 0 -maxdepth 8 -printf '%y %m %U:%G %p\n'
find -P /var/tmp/athena-d67-ext4 -xdev -mindepth 0 -maxdepth 8 -printf '%y %m %U:%G %p\n'

# After a separately reviewed exact file list, never broad-glob protected evidence
sha256sum -- /var/lib/athena-alpha-m1/systemd-canary.json
```

The documentation above intentionally omits install, canary, rollback, collector, cleanup, mount, snapshot, and test invocations: no such operation is approved by M17.

## 9. Gate decision

| Gate | M17 disposition |
|---|---|
| Source-level transaction reconstruction | **COMPLETE (static review only)** |
| Marker state from supplied audit | **BLOCKED / operator-reported**; incomplete inventory and zero-byte `install-started` conflicts with source. |
| Canary receipt bound to current candidate | **FAIL for binding**: both hashes diverge; historical origin unresolved. Receipt itself was not independently inspected. |
| D6 preservation | **PRESERVE**; do not touch. |
| Existing ext4 fixture reusable | **NO**: operator reports nonempty D6 subtree, while runner requires empty exact fixture root. |
| Current D6 metadata/custody qualification | **BLOCKED**: operator report is not raw independently reviewable evidence; previous historical access was blocked. |
| Authenticated candidate/build/inventory | **BLOCKED** per M16 evidence; no promotion. |
| Approved VM identity/readiness | **BLOCKED** absent current hypervisor-bound evidence. |
| Canary/install/privileged campaign/rollback | **NOT AUTHORIZED; NOT RUN**. |
| Alpha M1 qualification | **NO-GO / BLOCKED**. |

No PASS is claimed for any VM/runtime/administrative gate. A local static review cannot qualify systemd namespace behavior, mount state, crash recovery, VM identity, or D6 custody.

## 10. Precise source and evidence references

- Installer transaction/preflight: `qualification/install-athena-alpha-m1-admin.sh` lines 216–320, 323–475, 478–569, 572–693, 672–693, 713–859.
- Candidate pin literals: same installer lines 20–23 and 312–320.
- Runner path and fixture requirements: `qualification/athena-alpha-m1-runner.py` lines 24–46, 48–69, 108–170; execution/case lifecycle begins at line 316.
- Canary receipt binding and runtime checks: `qualification/athena-alpha-m1-systemd-canary.py` lines 24–31, 33–122, 125–186, 265 onward, 463 onward. No canary executed.
- Orchestrator exact case selection/result classification: `qualification/run-alpha-m1-privileged.py` lines 43–66, 90–140, 169–190. Not executed.
- Collector is ordinary-user only and exit 0 is not overall PASS: `qualification/alpha-m1-vm-evidence-collector.sh` lines 108–114, 179–180, 651 onward, 700–720, 898–903. Not executed.
- Current runbook transaction/marker and approval requirements: `docs/qualification/alpha-m1-session-34-admin-runbook.md` lines 9–53, 65–99, 101–105.
- Canonical fixture and current pin caveats: `docs/qualification/alpha-m1-admin-staging.md` lines 5–28, 30–49, 101–115, 144–146.
- D6 metadata-only/no-mutation procedure: `docs/qualification/alpha-m1-session-37-vm-procedure.md` lines 65–101; prior access denials and historical classifications: `docs/qualification/alpha-m1-session-37-report.md` lines 7–17, 47–60, 91–97.
- Prior gate and unresolved official inventory, pins, provenance, D6, VM and approvals: `docs/development/monad-0.1-alpha-m16-gate-review-20261028.md` sections “Tabella dei gate”, “D6 e prove conservate”, “Pin binari divergenti”, “Readiness VM”, and “Decisione finale”.
- Operator-supplied audit values and canary receipt in this report are reproduced only from the user message; they are not independently verified by this workspace review.
