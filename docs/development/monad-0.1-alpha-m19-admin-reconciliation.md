# Monad 0.1 Alpha M19 — Administrative installation reconciliation

**Gate: NO-GO. No installation, rollback, canary, collector, privileged test, or other VM operation was performed.** This report reconstructs the current checkout’s administrative installer and records the M18 VM data only as operator observations. It does not independently establish current VM state.

## 1. Executive summary

The installer’s three published administrative files are `/usr/local/sbin/athena-alpha-m1-runner`, `/usr/local/sbin/gateway_support.py`, and `/etc/sudoers.d/athena-alpha-m1`. The M18 checks of `/usr/local/libexec/athena-alpha-m1-runner`, `athena-alpha-m1-runner.py`, and the installed canary script do not match these publication destinations. An absent result at those paths says nothing about the actual runner/support targets. The canary script is a staged prerequisite; it is not installed by this installer.

The operator reports a transaction with `install-started`, `runner.absent`, and `sudoers.absent`, but no completion marker. In the current installer, an incomplete transaction blocks another install. A populated `install-started` has payload `transaction=<id>\n`; zero bytes do not match that implementation. `runner.absent` and `sudoers.absent` are expected zero-byte existence markers, but they only record original pre-install absence. Transaction and destination state remain **UNKNOWN** until a complete read-only acquisition reconciles all markers, manifests, saved originals, and actual destinations.

The operator-reported sudoers path is the actual destination. Its reported digest equals the current checkout sudoers draft and installer pin, and the policy is a fixed 18-command allowlist. This byte match does not establish how the installed file arrived, who staged it, or whether it is currently valid; no provenance or VM-side validation was independently obtained.

The current checkout runner, canary, and sudoers bytes match their literals in the installer. The reported historical receipt runner and canary hashes differ from the current pinned runner/canary. The receipt boot ID is historical per the operator and cannot satisfy the installer’s current-boot and 24-hour checks. The exact artifacts that generated that receipt are **UNKNOWN**. Do not alter pins or attempt a new canary until VM state, candidate identity, approvals, and prerequisites are reviewed.

## 2. Git revision and worktree state

**VERIFIED BY CODEX:** At inspection, branch was `main`, HEAD was `abd6bac4b368427b76f6c98522afde928b418939`, tracking `origin/main`. The worktree was substantially dirty: tracked changes in CMake/source/tests, and many untracked evidence, qualification, report, and build paths (including the requested report directory). These were treated as existing user work. No existing file was edited or removed. This report is the only file created for M19.

## 3. Administrative destinations and inventory reconciliation

### Actual source destinations

| Item | Exact path | Role and current knowledge |
|---|---|---|
| Runner | `/usr/local/sbin/athena-alpha-m1-runner` | Published as mode `0755`, root:root. **OPERATOR OBSERVATION:** not checked at this path in M18. |
| Gateway support | `/usr/local/sbin/gateway_support.py` | Published as mode `0644`, root:root. **OPERATOR OBSERVATION:** not checked at this path in M18. |
| Sudoers include | `/etc/sudoers.d/athena-alpha-m1` | Published as mode `0440`, root:root. **OPERATOR OBSERVATION:** present with stated digest. |
| Canary program | `/root/athena-alpha-m1-stage/athena-alpha-m1-systemd-canary.py` | Stage input only; not a published destination. Stage state is **UNKNOWN**. |
| Canary receipt | `/var/lib/athena-alpha-m1/systemd-canary.json` | Installer prerequisite. **OPERATOR OBSERVATION:** historical receipt present; not independently inspected. |
| State root | `/var/lib/athena-alpha-m1` | Installer expects protected root-owned state. **OPERATOR OBSERVATION:** root:root `0700`. |
| Backup root | `/var/lib/athena-alpha-m1/backups` | Contains transaction history and lock. **OPERATOR OBSERVATION:** one transaction directory described; full inventory unknown. |
| Install lock | `/var/lib/athena-alpha-m1/admin-install.lock` | Created/opened by installer for transaction serialization. **OPERATOR OBSERVATION:** not supplied. |
| Log directory | `/var/log/athena-alpha-m1` | Installer validates protected directory; rollback may remove it only when installer created it and it is empty. **UNKNOWN** in M18. |
| Fixture | `/var/lib/athena-alpha-m1/ext4-fixture` | Required empty root-owned `0700` ext4 directory. Operator reports nested D6 historical fixture content; preserve and treat as not ready. |
| Temporary publication files | Same parent directories as the three published files, names `.athena-alpha-m1-runner.*`, `.athena-alpha-m1-support.*`, `.athena-alpha-m1.*` | Normally renamed into targets; leftovers after interruption must be inventoried, not presumed disposable. State **UNKNOWN**. |
| Canary runtime artifacts | systemd unit `athena-alpha-m1-canary-<nonce>`, protected receipt lock and temporary/archival receipt paths | Runtime artifacts are separate from installed files. **OPERATOR OBSERVATION:** no loaded `athena-alpha-m1-*.service`; does not exclude failed, unloaded, archived, or on-disk evidence. |
| Runner runtime artifacts | `athena-alpha-m1-<nonce>.service`, per-case logs and fixture effects | Generated at runtime; inspect units, cgroups, logs, mounts, and fixture as evidence. **OPERATOR OBSERVATION:** no loaded units only. |

M18 incorrectly checked `/usr/local/libexec/athena-alpha-m1-runner`, `/usr/local/libexec/athena-alpha-m1-runner.py`, and `/usr/local/libexec/athena-alpha-m1-systemd-canary.py`. The installer writes the runner to `/usr/local/sbin/athena-alpha-m1-runner` and never installs the canary. The `.py` runner filename is the staging source name, not the installed target name.

### Complete read-only acquisition specification

Acquire on the named VM through an approved read-only administrative session, record UTC, VM/snapshot identity, boot ID, command identity, stdout/stderr and exit status, and preserve originals with custody metadata. This is a specification, **not an instruction executed in M19**.

1. **Identity/context:** VM UUID and hypervisor/snapshot evidence; machine ID; boot ID; kernel, Debian release, systemd version; UTC time; root session identity. Bind all captured outputs to the same session and boot.
2. **Path chain and metadata:** no-follow `namei`/`lstat`, type, mode, uid/gid, link count, ACLs and xattrs for `/usr/local/sbin`, the three exact installed targets, `/etc/sudoers.d`, `/var/lib/athena-alpha-m1`, `backups`, each transaction directory and every child, lock, receipt and archives, fixture and all children, `/var/log/athena-alpha-m1`, `/root/athena-alpha-m1-stage` and each expected stage file, `/opt/athena-qualification/build` and the seven pinned build artifacts. Also inspect the M18 libexec candidate paths strictly as extra paths, not substitutes.
3. **Digests/content:** SHA-256 for runner, support, sudoers, staged runner/support/sudoers/canary, installer, runner/canary source copies if present, protected pinned build files, receipt, all transaction JSON/manifest/backup payloads, and relevant logs. Read transaction records and marker bytes exactly. Preserve receipt archives and hidden temporary entries. Do not read unrelated fixture payloads; metadata inventory first.
4. **Transaction completeness:** enumerate the entire backup root including lock metadata. For each transaction capture exact directory entry set, no-follow metadata/ACLs, hashes and bytes for `transaction.json`, `backup-manifest.json`, `prepared`, all transaction markers, `{runner,support,sudoers}.{existed,absent}`, payloads and their `.mode`, `.sha256`, `.owner-group` sidecars, and `rollback-{runner,support,sudoers}.status`. Verify one existence marker per target, expected manifest schema and transaction identity/hash bindings offline.
5. **Runtime/policy:** read-only `visudo -c` is a policy validation check (not provenance); inspect sudoers include metadata and bytes; query loaded, failed and on-disk matching systemd units, unit properties/cgroups, process descendants, relevant journal intervals, mounts and namespace state. A current no-loaded-units observation is insufficient by itself.
6. **Preservation/fixture:** inventory `/var/tmp/athena-d67-ext4`, the historical fixture path, and the canonical fixture independently. Record filesystem/mount identity and child mounts without mounting or traversing payload contents. Reconcile whether reported D6 locations are distinct; do not rename, remove, clean, or repurpose anything.

Every item above is currently **UNOBSERVED BY CODEX** on the VM. M18 facts remain **OPERATOR OBSERVATION**, not verified findings.

## 4. Transaction state machine

The current source implementation is reconstructed as follows:

1. **Preflight and serialization:** validates pinned stage files and protected paths, fresh receipt, ext4 empty fixture, protected build hashes and tool availability; creates/locks `admin-install.lock`; refuses any backup transaction without `install-complete` or `rollback-complete`, and any rollback started without rollback complete.
2. **Original capture:** creates a unique transaction directory; for each of runner/support/sudoers, saves original bytes and mode/hash/owner-group sidecars if present, or creates the corresponding `.absent` marker if absent. The existence marker itself is created as an empty file. Writes/fsyncs aggregate `backup-manifest.json`, canonical `transaction.json`, and `prepared` marker before publication.
3. **Candidate preparation:** creates temporary files in each destination’s parent, sets expected owner/mode, copies candidate bytes, validates sudoers syntax, and fsyncs staged content.
4. **Publication boundary:** creates `install-started` containing `transaction=<directory-name>\n`, fsyncs the file and transaction directory. This is the commit-to-publication boundary, not an all-targets commit.
5. **Three separate renames:** runner, support, then sudoers are individually renamed and each parent directory fsynced. Interruption can leave any prefix published, including all three published but verification/completion not recorded.
6. **Completion:** verifies target hashes and ownership/modes, validates sudoers, then persists `install-complete`. That marker is the installer’s successful transaction terminal marker; it still requires independent current-destination verification.
7. **Failure:** shell exit trap after `install-started` attempts `install-partial` with transaction and exit status, preserving destination state and backups. Before `install-started`, temp candidates are removed. SIGKILL/power loss can leave no partial marker.
8. **Explicit rollback:** requires transaction identity and a separate approval token bound to transaction/backup manifest hashes; validates transaction, manifest, saved artifacts, destination identities and safe metadata before touching targets. It writes `rollback-started`, restores/removes each target sequentially according to recorded original presence, records per-file status, validates sudoers, then writes `rollback-complete`. Failures can leave mixed destinations and `rollback-partial`; source blocks retry after rollback has started. Manual recovery is then required.

Thus `install-started` alone is indeterminate. No atomic commit covers all three destinations. Backup data and historical evidence must be retained.

## 5. Marker semantics

| Evidence | Source meaning | M18 interpretation |
|---|---|---|
| `prepared` | Original backups, aggregate manifest, transaction record persisted; candidate preparation is reported complete. Not publication. | Not reported: **UNKNOWN**. |
| `install-started` | Durable boundary before first rename; destinations can be old, new, or mixed. Must contain exact transaction ID line. | Operator says zero bytes. **Mismatch with current code**, requiring raw-byte/metadata review and possible installer-version identification. Does not prove whether any rename occurred. |
| `install-partial` | Exit trap observed failed post-boundary install and recorded transaction plus numeric exit. Can be missing after SIGKILL/power loss. | Not reported: **UNKNOWN**. |
| `install-complete` | Installer completed publication and validations. Independent target verification still needed. | Reported absent: operator observation only. |
| `rollback-started` / `rollback-partial` / `rollback-complete` | Explicit restore attempt; partial or absent terminal state may mean mixed generations. | Reported rollback-complete absent; other rollback markers **UNKNOWN**. |
| `runner.existed` / `runner.absent`, `support.existed` / `support.absent`, `sudoers.existed` / `sudoers.absent` | Original target presence during backup capture; `.absent` and `.existed` marker files are empty. Exactly one per target. Not current presence. | Reported runner/sudoers absent markers are consistent with source format but do not establish current target state. Support marker **UNKNOWN**. |
| `transaction.json`, `backup-manifest.json`, payloads/sidecars/status files | Bind transaction ID, destinations, original states, candidate hashes, backup inventory and rollback statuses. | Not supplied; cannot validate transaction identity or recoverability. |
| Canary receipt | Evidence of a canary result bound to hashes/boot/systemd/time. It is not an install marker. | Historical boot per operator; cannot satisfy current installer prerequisite. |

The user-reported zero-byte `install-started` is not the expected format in the current script. Possible explanations (old code, truncation, altered file, transcription error) are **INFERENCE only**, not established. Do not repair or normalize markers.

## 6. Sudoers reconciliation

Current draft `qualification/athena-alpha-m1-sudoers` has SHA-256 `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18`; installer literal matches. **VERIFIED BY CODEX:** local draft bytes hash to that digest. **OPERATOR OBSERVATION:** installed `/etc/sudoers.d/athena-alpha-m1` reportedly has the same digest and mode/owner root:root `0440`.

The source policy defines `ATHENA_ALPHA_M1` as exactly 18 invocations of `/usr/local/sbin/athena-alpha-m1-runner` with fixed test names, and grants `alessandro ALL=(root) NOPASSWD` for that alias. It has no wildcard or shell command in the draft. This is a narrow command allowlist, but invocation safety also depends on runner bytes, argument enforcement, sudoers include validity and host configuration.

Equal SHA-256 supports byte equality to the current draft, assuming reliable acquisition, but **does not establish provenance**, staging chain, authorized installation, current path integrity, ACLs, include activation, syntax validity, or absence of other sudo policy. Do not modify or activate sudoers. A read-only `visudo -c` plus complete include inventory and custody chain are still required before any recovery decision.

## 7. Canary and pin reconciliation

| Artifact | Checkout bytes / installer literal | M18-reported receipt | Assessment |
|---|---|---|---|
| Runner | `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` | `5d6ee35070954d97034de7f0323204995fe0ca86ed12fad774a3acb9ada021d5` | **VERIFIED BY CODEX:** checkout equals installer pin. Receipt runner differs. |
| Canary | `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` | `5919ef00cda396afb71c0c772fdf5814c98fdcebac30c73dc0f468721502a148` | Checkout equals installer pin; receipt canary differs. |
| Sudoers | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` | Not bound by receipt | Checkout equals installer pin; reported installed digest equals it, provenance unknown. |
| Gateway support | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` | Not bound by receipt | Installer pin documented; VM destination not reported. |
| Build artifacts | Seven fixed hashes in installer, including six test/library binaries and `CTestTestfile.cmake` | Not bound by receipt | VM artifact hashes and authenticated provenance not supplied in M18. |

M18 also says the receipt boot ID differs from current boot. The installer requires schema 1, passed=true, exact canary and runner hashes, receipt age ≤24 hours, no future timestamp beyond tolerance, current boot ID, and current systemd version. Therefore this receipt fails at least hash binding and current-boot checks by operator report. Its actual age and raw structure remain unverified. Historical reports associate the `5d6ee…` runner with earlier records, but they do not prove which bytes produced the receipt. Exact historical canary provenance is **UNKNOWN**.

The installer itself currently hashes `2376f43c…` in this checkout, but that identity is not a self-pin inside the installer and must be bound by a separately reviewed candidate manifest. Session 34B historical documentation says runner/support/canary/sudoers and installer pins were reviewed; it describes repository candidate identity, not current VM staging or installation. No pin was modified or promoted here. Any source-pin divergence or candidate change must use explicit reviewed promotion.

## 8. Recovery decision tree

No branch was executed. Each state-changing branch needs a human administrator and the exact transaction/VM/boot identity documented in a separately reviewed authorization. Preserve all backups, logs, receipts, D6 fixture and current targets.

1. **Completed installation candidate:** requires raw `install-complete`, canonical transaction/manifest validation, exact three destination hashes/metadata matching that transaction’s candidates, valid sudoers, stage/candidate provenance, current receipt and boot prerequisites, and no unexplained temp/runtime artifacts. If all agree, classify as completed but do not treat it as qualification. Human approval is still required for any subsequent canary or privileged test. If installed bytes differ, go to unknown/inconsistent.
2. **Interrupted before publication:** only if `install-started` is absent, verified transaction is prepared, no destination differs from captured original, and only safely attributable temp files exist. Source refuses transaction reuse and does not provide automatic resume. Preserve and review; human-approved manual disposition or newly authorized transaction only after candidate/VM gates. Do not delete temporary files as part of acquisition.
3. **Interrupted/partial publication:** `install-started` without valid terminal marker, any `install-partial`, or target mix of original/candidate. Freeze state. Acquire complete transaction and destination evidence; verify manifest, original snapshots, candidate hashes and approval policy. Choose explicitly between separately authorized rollback and bespoke manual recovery. Do not rerun install.
4. **Rollback required:** requires authenticated original-state backup, valid aggregate manifest and transaction, destination bytes each equal to recorded original or candidate generation, safe regular files/no-follow path metadata/ACLs, and a reviewed exact rollback token bound to transaction and manifest. Human approval must name rollback and target VM/boot. Rollback is sequential and may itself become partial; independent post-rollback verification and retained evidence are mandatory. No approval/token is present in M18.
5. **Rollback started/partial, manifest failure, unexpected destination, missing or conflicting markers, unknown installer version, zero-byte `install-started`, unexpected entry/link/ACL, or inaccessible evidence:** classify **INCONSISTENT/UNKNOWN**. No automated installer rollback is safe. Preserve everything, stop administrative changes, escalate for independent forensic review and a written manual recovery plan grounded in acquired evidence. No recovery action is specified until the discrepancy is resolved.

## 9. Missing evidence and minimal next read-only acquisition

### Exact gaps

- Raw M18 command outputs, timestamps, exit statuses, collection method, identity and custody are absent; all VM facts remain operator observations.
- Actual three publication targets were not checked; only non-destination `/usr/local/libexec` names were reported absent.
- Full transaction directory is unknown: `prepared`, `transaction.json`, `backup-manifest.json`, support existence marker, payloads/sidecars, partial/rollback markers/status files, exact entry set, metadata, ACLs and hashes.
- Actual runner/support/sudoers bytes and metadata at source destinations are unknown; sudoers digest claim lacks independently acquired raw digest output and provenance.
- Stage contents, transaction lock, logs, hidden temporary files, receipt archives/lock, systemd on-disk units, failed units, cgroups/processes/journal and fixture/mount effects are unknown.
- Protected build artifact hashes, CTest inventory, candidate manifest/provenance, VM/snapshot binding and current receipt JSON timestamp are not present in raw evidence.
- Relationship and identity of `/var/tmp/athena-d67-ext4` and the nested historical fixture cannot be independently established from supplied statements.

### Minimal next read-only acquisition

Obtain a single custody-preserved, non-mutating VM capture with: (a) VM UUID/snapshot, boot ID and UTC; (b) no-follow metadata/ACL and SHA-256 of exact actual runner/support/sudoers paths plus the three mistakenly checked libexec paths; (c) complete no-follow inventory and exact bytes/hashes of the named transaction directory and all its entries, including transaction/manifest, markers, backups/sidecars/status; (d) receipt JSON/hash/metadata plus archive and lock inventory; and (e) state-root, backup-root, lock, logdir, stage and fixture metadata/mount identity. Do not run the collector or installer. This is the smallest useful acquisition to resolve whether the reported paths and transaction form a coherent account; extend to build/runtime inventories before any recovery authorization.

## 10. Readiness and gate decision

| Gate | Decision |
|---|---|
| Static source/destination reconstruction | **COMPLETE (checkout only)** |
| M18 installed runner/support status | **UNKNOWN**; wrong paths checked |
| M18 sudoers bytes | **OPERATOR OBSERVATION** matching local draft digest; provenance and syntax/inclusion not independently established |
| Transaction status | **INDETERMINATE / BLOCKED**; incomplete markers and zero-byte start-marker discrepancy, missing complete transaction inventory |
| Receipt binds current candidate/current boot | **NO** by reported hashes and boot ID; raw receipt not independently verified |
| Current VM identity/build/stage/runtime state | **BLOCKED / UNKNOWN** |
| Recovery permission | **NOT PERMITTED** until evidence reconciliation, independent review, validated backup and explicit human approval for exact branch |
| New canary | **NOT READY** until VM identity/current boot, selected reviewed exact canary+runner hashes, staged bytes/provenance, runtime prerequisites, and historical receipt preservation are established; then separate explicit authorization for execution and receipt destination |
| Privileged qualification | **NOT READY** until recovery/install state is resolved, installed hashes and sudoers are independently verified, build and exact test inventory authenticated, canary passes on same boot with current hashes, D6/fixture preservation and runtime cleanup evidence are accepted, and campaign authorization is explicit |
| Alpha M1 qualification | **NO-GO**. No privileged campaign PASS is asserted. |

**Criteria for permitting recovery:** complete custody-preserved inventory; known installer/version and transaction identity; canonical validated manifest and transaction bindings; safe and attributable backups; all three actual destinations reconciled to original/candidate bytes; raw marker discrepancy explained; independent operations/security review; explicit approval naming action, VM/snapshot/boot and evidence hashes; rollback/manual recovery steps and stop conditions recorded.

**Criteria for permitting a new canary:** recovery/installation uncertainty first resolved; candidate hashes frozen through reviewed process; stage and source provenance verified; VM/snapshot/boot and systemd version confirmed; read-only preflight validates state paths, tools, receipt archival state and runtime prerequisites; D6 preserved; separate approval names exact canary and runner hashes, VM/boot, command and receipt path. A canary result authorizes neither installation nor privileged tests by itself.

**Criteria for permitting privileged qualification:** all install targets and sudoers proven correct at real destinations; sudoers syntax and complete policy inventory reviewed; active receipt fresh and bound to same runner/canary/boot/systemd; authenticated protected build hashes and exactly the approved 18-case inventory; complete preservation and fixture readiness evidence; independent review of prior transaction/recovery; explicit campaign approval and custody plan. Every expected case and cleanup evidence must later be reviewed before qualification.

## 11. Checks executed and source references

**VERIFIED BY CODEX checks:** `git status --short --branch`; `git rev-parse HEAD`; `git branch --show-current`; static `sed`/`nl`/`rg` inspection; `sha256sum` of repository runner, canary, sudoers draft, and installer. No tests or builds were run. No installer, runner, collector, canary, privileged program, sudo, or VM command was executed. Hashes are local checkout observations only.

Primary source references (line numbers refer to this checkout):

- Installer destination and digest literals: `qualification/install-athena-alpha-m1-admin.sh` lines 8–23; test-root redirection lines 25–47.
- Backup inventory and marker payload validation: installer lines 360–469; `.existed`/`.absent` capture and sidecars in lines 777–810; marker writer lines 688–693.
- Fixture and receipt requirements: installer lines 216–272; all installer prerequisites and seven build hashes lines 291–320.
- Transaction record and rollback token binding: installer lines 572–669; rollback target restore and sequential status handling lines 492–569.
- Exit trap, install lock, incomplete transaction guard, installed verification and publication sequence: installer lines 672–693, 713–754, 777–859.
- Runner runtime unit naming and fixed runner path consumer: `qualification/athena-alpha-m1-runner.py` lines 24–46 and 345–410; `qualification/run-alpha-m1-privileged.py` line 14.
- Canary canonical receipt, archive/lock, hashes and runtime unit: `qualification/athena-alpha-m1-systemd-canary.py` lines 24–40, 210–310, 460–550.
- Sudoers 18 fixed commands and grant: `qualification/athena-alpha-m1-sudoers` lines 1–22.
- Administrative sequencing and historical install/rollback procedure: `docs/qualification/alpha-m1-session-34-admin-runbook.md` lines 9–31, 65–105; note that its statement of best-effort automatic rollback at line 101 conflicts with the current installer trap at lines 672–685, which preserves post-boundary state and does not restore targets.
- Current staged-artifact/fixture and manual evidence cautions: `docs/qualification/alpha-m1-admin-staging.md` lines 28–75, 89–105, 119–146.
- M17 prior D6 reconciliation and supplied-evidence distinctions: `docs/development/monad-0.1-alpha-m17-d6-reconciliation.md` sections 2–5, 7–10.
- Rootless transaction behavior is represented by static test source `tests/alpha_m1_installer_transaction_test.py` lines 129–220 and 233–260. **The tests were not run.**

Classification key: **VERIFIED BY CODEX** means directly inspected or computed from local checkout; **OPERATOR OBSERVATION** means supplied in M18 but not independently acquired; **DOCUMENTED HISTORICAL CLAIM** means prior repository record, not current VM fact; **INFERENCE** means reasoned possibility, not proof; **UNKNOWN** means absent or unresolved evidence.
