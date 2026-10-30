# Monad 0.1 Alpha M27 — forensic reconciliation

**Verdict: NO-GO.** M27 advances the local artifact comparison: all seven protected build files under `/opt/athena-qualification/build` now hash exactly to the seven historical expected pins, resolving their observed byte mismatch at that path. This is content equality, not authenticated custody or proof that the VM ran those bytes. The supplied guest acquisition is checksum-consistent and materially improves the administrative picture, but its transaction tree is incomplete and its empty `install-started` marker conflicts with the current writer. No install, rollback, canary, VM command, privileged test, or evidence mutation was performed.

## 1. Evidence classes and scope

This report uses these labels:

- **LOCAL VERIFIED:** directly read or hashed from this checkout/container during M27.
- **OPERATOR-ACQUIRED:** content in the supplied M27 bundle. Its checksum verifies internal consistency only. No independent authentication, operator identity, transport chain, trusted timestamp, or VM-side signature was supplied.
- **HISTORICAL CLAIM:** prior report or record; it establishes what that record says, not necessarily the underlying event.
- **INFERENCE:** a consequence consistent with observed bytes/source, without proof of a causal event.
- **UNKNOWN / NOT OBSERVED:** absent from the accessible evidence.

The repo was `main`, `HEAD=abd6bac4b368427b76f6c98522afde928b418939`, tracking `origin/main`, at inspection. The worktree was already extensively dirty with tracked edits and untracked reports, scripts, build directories, and evidence. Those were preserved. The new report was written to a previously absent path.

## 2. Checksum and M27 acquisition contents

**PASS (internal checksum):** from `/home/alessandro/monad-m27-evidence-20261029T161836Z`, `sha256sum -c acquisition.sha256` reported `acquisition.txt: OK`. The checksum file covers `acquisition.txt`; it does not authenticate the source or acquisition process.

The acquisition timestamp is `2026-10-29T16:18:36+00:00`. It describes Debian GNU/Linux 13 (trixie), x86-64, VirtualBox/Oracle, hostname `debian`, machine ID `9bdbfd8678b5467cb85e4d8aa5e01fea`, product UUID `91ea75e4-e972-5741-9ee6-583e9ae0462f`, and boot ID `58fa9502-d0d3-4b02-b57b-82666e5a2b2b`. These are **OPERATOR-ACQUIRED claims**, not independently observed VM identity.

### M27 reported paths and markers

| Path | Acquisition observation | Contract reconciliation |
|---|---|---|
| `/usr/local/sbin/athena-alpha-m1-runner` | regular, root:root, mode 0755, 15,990 bytes; SHA-256 `5d6ee35070954d97034de7f0323204995fe0ca86ed12fad774a3acb9ada021d5` | Correct installed runner path. Hash differs from current pin/source (`c68b…9def`). |
| `/usr/local/sbin/athena-alpha-m1-runner.py` | absent | Not an installed destination in current source; its `.py` name is the staged input. |
| `/usr/local/sbin/gateway_support.py` | absent | This is the actual support destination. Unlike earlier reports whose acquisition path coverage was incomplete, M27 explicitly reports it absent. |
| `/etc/sudoers.d/athena-alpha-m1` | regular, root:root, mode 0440; SHA-256 `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` | Correct destination and digest equals current source/pin. Syntax and policy validation were not acquired. |
| `/var/lib/athena-alpha-m1/backups/run-20261021-061921/` | mode 0700 root:root; only `sudoers.absent`, `runner.absent`, and zero-byte `install-started` are shown | Current protocol expects more entries and a non-empty start marker. M27 does not provide a full tree listing. |
| `/var/lib/athena-alpha-m1/systemd-canary.json` | mode 0600, root:root; SHA-256 `863872f25ae39a2edf39958fa14ac6c69f0faede06f759f3c68be44198181026`; JSON says passed | Receipt is stale and boot/hash mismatched for current contract (see §7). |
| `/var/tmp/athena-d67-ext4` | empty directory metadata only | Does not establish original D6 custody or content history. Preserve. |
| `/var/lib/athena-alpha-m1/ext4-fixture/athena-d6-8yRDjc/...` | nonempty nested historical fixture tree with lock file and directories | Not eligible for the current empty-fixture prerequisite. Preserve; do not traverse payloads or reuse. |

Also reported: systemd running and no loaded units in two listings. The acquisition does not include failed/on-disk unit inventory, process/cgroup state, mount namespace detail, journal, ACLs, link counts, transaction file bytes except the three stated empty hashes, or a complete path listing. Do not infer their absence.

## 3. Installer contract and omitted evidence

The current administrative script is `qualification/install-athena-alpha-m1-admin.sh`:

- Inputs and pins: lines 8–23.
- Actual destinations: runner `/usr/local/sbin/athena-alpha-m1-runner` (line 13), support `/usr/local/sbin/gateway_support.py` (line 14), sudoers `/etc/sudoers.d/athena-alpha-m1` (line 15). Canary is staged input at line 12; it is not installed.
- State paths: log directory, fixture, backup root, receipt at lines 16–19.
- Fixture must be ext4, root-owned mode 0700 and empty: lines 216–228.
- Receipt must be root-owned mode 0600 and match current hashes, current boot, current systemd version, and 24-hour freshness (future skew at most 300 seconds): lines 230–272.
- Backup captures each of runner/support/sudoers as `<key>.existed` plus payload/sidecars or `<key>.absent`: lines 790–806. The manifest/transaction/prepared marker follow at lines 807–810.
- Candidate files are staged beside each destination with prefixes `.athena-alpha-m1-runner.`, `.athena-alpha-m1-support.`, and `.athena-alpha-m1.` and then individually renamed: lines 811–838.
- `install-started` is written by `mark_once` as `transaction=<backup-directory-leaf>\n` and fsynced (lines 688–693, 823–825). Its location is before the first rename; it does not mean publication completed.
- Publication order is runner, support, sudoers, each followed by parent-directory fsync. Completion follows all three and validation (lines 827–842). These renames are not one atomic transaction.
- A normal caught post-boundary failure attempts `install-partial` with transaction ID and exit code (lines 672–685); SIGKILL/power loss may prevent it. New installs refuse an incomplete transaction (lines 728–739). Rollback is a distinct token-authorized action and sequential per-file restore (lines 756, 846–858).

### M27 deviations and acquisition omissions

1. `install-started` is reported empty (`e3b0…b855`), whereas current code writes a transaction line. That is a real mismatch with the current script bytes, but does not prove which historical script ran or whether the marker was truncated later.
2. `runner.absent` and `sudoers.absent` are consistent with the observed current presence of runner/sudoers: these markers describe **pre-install** state, not present state.
3. Current installer captures all three targets. M27 does not show `support.absent` or `support.existed`; yet the destination is reported absent now. This prevents reconciling the support generation and original state.
4. Missing from the presented transaction tree: `prepared`, `backup-manifest.json`, `transaction.json`, original payload and sidecars for any `.existed` target, `install-partial`, `install-complete`, rollback markers/statuses, hidden entries, and `admin-install.lock`. The acquisition may have omitted them rather than verified their absence.
5. Candidate staging files, installed support hash (not applicable if truly absent), current path ACL/xattr/link data, sudoers `visudo` result, installer identity/hash on VM, logs, audit/journal and temporary siblings are not included.
6. Current source’s incomplete-transaction guard would block a new install for this transaction as described, because neither completion terminal marker is reported. No rerun is safe.

## 4. Artifact matching and provenance

### Current local source and installer pins

`sha256sum` over current local files gives:

| Artifact | Local SHA-256 | Current installer pin | Result |
|---|---|---|---|
| `qualification/athena-alpha-m1-runner.py` | `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` | same | match |
| `qualification/gateway_support.py` | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` | same | match |
| `qualification/athena-alpha-m1-sudoers` | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` | same | match |
| `qualification/athena-alpha-m1-systemd-canary.py` | `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` | same | match |
| `qualification/install-athena-alpha-m1-admin.sh` | `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7` | no self-pin in script | local identity only |

Pins are at installer lines 20–23. M27 runner digest differs from current runner pin. Its sudoers digest matches current. Support absence means no installed support digest exists in supplied evidence.

### Seven protected build artifacts

M27 local read-only hashing of `/opt/athena-qualification/build` found all seven files, each **exactly equal** to the historical expected pin recorded in M16 §“Pin binari divergenti” (M16 lines 70–84):

| Artifact | Expected and current protected-path SHA-256 | M27 result |
|---|---|---|
| `activation_execution_privileged_test` | `d87a84cbb1e18ba073beaadb1bb7737b61fa448081af37e4d4c5a6c29337d157` | exact match |
| `activation_plan_retention_test` | `f76251d6e0af0bd892d1dcb17f41f80b13343c7c274e289014dfcb090370e474` | exact match |
| `activation_plan_persistence_test` | `f12620d89bb55e66f1e1960dd8852f2fdf27c9a148967d222d562fdddbd5aaa3` | exact match |
| `legacy_d3_test` | `61e23e8c9cd3a81c8766eb61e93e01cc7c9af2025a80f6056f28d3ac137ad6a9` | exact match |
| `initial_activation_test` | `934222dc181ae1a98a3d00bed5cba8e208cdad194d0c10fe552b1e5cf7e74243` | exact match |
| `libathena_core.so` | `e212420cb34aeca76cebe2ecf396832f25c3c4f119b846b42ef00d2994cc995a` | exact match |
| `CTestTestfile.cmake` | `99751015487882864d67b948dbbb0eee4ac7204aca1b7ee20435f8e69b656bd5` | exact match |

This newly resolves **local byte equality**; it supersedes the prior M14 local hashes for this path. It does not explain how/when these protected files arrived, whether the directory is the protected acquisition intended by the earlier reports, whether its metadata/ACL/custody is trustworthy, or whether those exact files were executed on the guest. No binaries were executed. No pin was changed.

### Historical runner identity

The M27 installed runner hash `5d6ee350…021d5` equals the receipt’s `runner_sha256`. It differs from current source and pin. A local Git object/path scan found no repository object for `qualification/athena-alpha-m1-runner.py` or installer source from which this digest could be tied to a revision. Earlier M19/M24 records also called the installed digest unidentified. Therefore the historical runner is **not associated with an identifiable revision** from available evidence. Receipt hash equality is content linkage inside the supplied record, not provenance; no exact release bundle, archived source, signed build attestation, VM stage copy, or custody chain was supplied.

## 5. Transaction reconstruction

Observed reported state: runner present; sudoers present; support absent; two original-absence markers; empty start marker; no completion marker shown. The following remain plausible and not mutually exclusive:

| Hypothesis | Why it remains possible | Evidence that could distinguish it |
|---|---|---|
| Current-like interrupted publication after runner rename | Current order publishes runner first. An interruption could leave runner updated while later targets remain old/absent. The empty marker conflicts with current writer. | Raw full backup tree, source version, hashes of all siblings/temp files, journal/process/audit records, and stage/build bytes. |
| Interruption before any rename, followed by later/manual runner and sudoers placement | `install-started` ordinarily precedes first rename; by itself it proves no rename. Later changes could explain current destinations. | Trustworthy time-bound audit/configuration-management/shell records and inode/change metadata; hash evidence alone cannot identify actor. |
| Historical installer version with another marker schema or target set | Empty marker may be valid under different code. Current source cannot retroactively define historical behavior. | Exact archived installer bytes/hash, VM staged copy/package, release bundle, historical transaction schema and matching code. |
| Marker was truncated/rewritten after a transaction | Present empty file is compatible with later alteration. | Filesystem/journal/audit backup snapshots or immutable external logs with custody; current metadata alone is insufficient. |
| All targets were published then support was removed or reverted | Current runner/sudoers presence and support absence may be post-install changes. | Historical target snapshots, transaction manifest candidate hashes, audit/configuration records. |
| Acquisition omitted other transaction files | The text shows a selected tree listing, not proof of exhaustive enumeration. | New full no-follow listing and raw bytes for every transaction entry, including hidden names and lock. |

No event is inferred as fact. The three files are published separately, so “runner present” plus “sudoers present” does not establish transaction completion. A valid current manifest would require exactly one original-presence marker per each of runner/support/sudoers and consistent records; the supplied partial listing is insufficient to evaluate this.

## 6. Canary readiness

Receipt claims `passed=true`, canary hash `5919ef00cda396afb71c0c772fdf5814c98fdcebac30c73dc0f468721502a148`, runner hash `5d6ee350…021d5`, boot `3fec68c2-2d94-45ea-b574-00249796f987`, systemd `257.13-1~deb13u1`, and `validated_ns=1792563176378053471` (= `2026-10-21T06:12:56.378053+00:00`). At M27 acquisition it is about 8 days old, exceeding current 24-hour maximum. Its receipt boot differs from M27 boot `58fa9502-…`; its runner/canary hashes differ from current pins. The acquisition does not state current systemd version, so that comparison is **UNKNOWN**. Current installer requires fresh receipt, current boot, exact systemd line, and exact canary+runner pins (`install-athena-alpha-m1-admin.sh:230-272`).

**Assessment: NOT READY / stale for current installer.** No canary was run. Historical systemd “running/no loaded units” observations do not satisfy current receipt validation or establish runtime cleanup. Preserve the receipt and any archives; do not replace it as part of this work.

## 7. D6 readiness

M27 newly supplies direct operator-acquired metadata for two distinct locations: `/var/tmp/athena-d67-ext4` is shown as an empty directory, while `/var/lib/athena-alpha-m1/ext4-fixture` contains nested `athena-d6-8yRDjc` directories and a zero-byte state lock. This helps reconcile that the historical fixture is not the current installer fixture path. It does not prove D6 parent/source, mount identity, custody, test completion, or that `/var/tmp` is the authoritative preserved D6 artifact. The installer’s canonical fixture must itself be ext4, root-owned 0700, and empty (lines 216–228); the reported nested fixture fails the empty prerequisite if it is that path. Do not mount, rename, delete, clean, traverse payloads, or reuse either location.

**Assessment: historical fixture preservation partially documented; original D6 custody and canonical fixture readiness remain UNKNOWN / NOT READY.**

## 8. Recovery decision tree (documentation only)

No command below was executed. Every state-changing branch requires a separately reviewed root-owned, hash-pinned artifact, exact argument allowlist, named VM/snapshot/current boot, retained evidence copy, independent review, and explicit human authorization. Recovery does not qualify Alpha M1.

1. **Indeterminate / current M27 branch (now):** stop. Preserve acquisition, receipt, backups, fixture and logs. Obtain one complete read-only acquisition tied to VM UUID, machine ID, boot ID, UTC and operator identity. Include all three destinations, stage files, every backup entry/raw marker, manifest/transaction/payloads/sidecars, lock, logs, temp siblings, receipt/archive, installer hash, ACL/xattrs, units/processes/cgroups/mounts and D6 metadata. No installer, rollback, canary, or marker repair.
2. **Transaction proven complete:** require valid `transaction.json` and `backup-manifest.json`, coherent expected markers, all three target hashes/metadata match the approved generation, sudoers validation and policy review, and no unexplained temp/runtime state. Action: no mutation; retain backups and record reviewed completion. Any discrepancy returns to branch 1.
3. **No publication proven and original state intact:** require exact historical writer semantics, complete backup, and all targets match recorded originals/absence with no runtime effects. Action: no repair and no rerun in this session. A new install can be considered only as a separate approved change after candidate, receipt, fixture, provenance and qualification gates close, using a fresh unique backup directory.
4. **Mixed publication with valid recoverable originals:** require a validated manifest, authenticated original payloads and sidecars, exact current targets, source/candidate identity, no concurrent operation, recovery snapshot, and explicit decision to restore. After authorization only, command shape is:

   ```sh
   sudo /path/to/reviewed/install-athena-alpha-m1-admin.sh rollback \
     /var/lib/athena-alpha-m1/backups/TRANSACTION_ID \
     --transaction-id TRANSACTION_ID \
     --approval-file /path/to/approved-rollback-token.json \
     --approval-sha256 APPROVED_TOKEN_SHA256
   sudo /usr/sbin/visudo -c
   ```

   The rollback token must bind the exact transaction/manifest. Then independently reacquire all destination hashes, owner/mode/ACL, transaction markers and sudoers validation. If any command fails, completion marker is absent, or any target differs from expected: stop, preserve partial state, no retry/manual edits; require a new reviewed recovery decision.
5. **Invalid/missing backup, historical writer unidentified, or acquisition contradiction:** remain NO-GO; preserve all. Do not synthesize markers, copy checkout artifacts into place, or delete state. Expand only through approved read-only evidence collection and independent forensic review.

## 9. Qualification gate table

| Gate | M27 status | Basis |
|---|---|---|
| 16 official inventory gaps | **OPEN** | M14/M15/M16 record 16 unlisted source/test inputs and qualification driver FAIL. M27 did not modify official inventory or rerun driver. M14 details: `monad-0.1-alpha-m14-timeout-reconciliation-20261028.md:72-116`. |
| Seven binary pin mismatches | **PARTIALLY RESOLVED** | All seven protected files at `/opt/athena-qualification/build` now exactly match historical pins. Local custody, build provenance and executed-file link remain unverified. |
| Source/build provenance and custody | **OPEN** | No signed/independent provenance chain, approved closure promotion, or reproducible-build review. M15/M16 candidate records remain local evidence. |
| Administrative transaction | **OPEN** | Correct actual runner/sudoers paths observed; support absent; marker malformed for current contract; full backup tree and installer version unknown. |
| Canary | **OPEN** | Receipt is passed by its own content but stale (>24h), old boot and old candidate hashes; current systemd version not acquired. No new canary. |
| D6 / fixture | **PARTIALLY RESOLVED** | M27 distinguishes an empty `/var/tmp` directory from a nonempty historical fixture tree; custody, mount/source identity and canonical fixture readiness remain unknown. |
| Privileged tests / scanner campaign | **OPEN** | No tests/canary/runner/collector/privileged qualification executed in M27. Required result set remains unproven. |
| Independent review | **OPEN** | This reconciliation is not independent acceptance of acquisition provenance or release evidence. |
| VM identity/custody | **UNKNOWN** | Identity fields are present in supplied acquisition, but acquisition provenance is unauthenticated and no snapshot/chain/independent identity binding is supplied. |
| Systemd/process/cgroup/mount state | **UNKNOWN** | Only running/no-loaded-unit summary and partial directory metadata supplied. |

## 10. Verification log

- **PASS:** `git status --short --branch`, `git rev-parse HEAD`; no existing paths were changed.
- **PASS:** acquisition checksum verified with `sha256sum -c acquisition.sha256` from the evidence directory.
- **PASS:** local SHA-256 calculated for four pinned source inputs and installer; values listed in §4.
- **PASS:** local SHA-256 calculated for all seven protected build artifacts; all seven matched the historic expected values listed in M16.
- **PASS:** read-only report/source/history inspection for M14–M16, M17, M19, M24, M26 and current installer.
- **NOT RUN:** VM-side commands, canary, installer, runner, rollback, collector, privileged tests, `visudo`, builds, CTest, qualification driver, systemd/mount changes. No FAIL/SKIP/TIMEOUT outcome is claimed for these because they were not run.
- **NOT RUN:** independent provenance/custody review and snapshot verification.

## 11. Verdict and minimal next action

**NO-GO** for installation, rollback, canary, privileged campaign, or Alpha M1 qualification. New findings are the internally checksum-consistent M27 acquisition, explicit report of the true support destination as absent, separation of the empty `/var/tmp` location from the nonempty historical fixture, and exact local matches for all seven previously divergent protected build pins. These reduce uncertainty but do not close the transaction, canary, D6 custody, source inventory, provenance, privileged campaign, or independent-review gates.

**Minimal next action:** commission a complete, identity-bound, read-only VM acquisition of the entire administrative transaction and actual target set, stage and receipt, systemd/process/cgroup/mount state, logs, and D6/fixture metadata, with raw output and custody metadata preserved; obtain independent forensic review before any recovery or qualification authorization.

## 12. Source and historical references

- Current installer destinations and pins: `qualification/install-athena-alpha-m1-admin.sh:8-23`.
- Fixture and receipt contract: `qualification/install-athena-alpha-m1-admin.sh:216-272`.
- Backup markers and manifest: `qualification/install-athena-alpha-m1-admin.sh:397-467,790-810`.
- Publication, marker writer, failure handling and completion: `qualification/install-athena-alpha-m1-admin.sh:672-693,728-754,811-858`.
- Prior inventory gap and reconciliation: `docs/development/monad-0.1-alpha-m14-timeout-reconciliation-20261028.md:72-116`.
- Candidate release evidence and seven pin findings: `docs/development/monad-0.1-alpha-m15-release-evidence-20261028.md:48-132`; `docs/development/monad-0.1-alpha-m16-gate-review-20261028.md:70-84,112-131`.
- Prior D6 reconciliation: `docs/development/monad-0.1-alpha-m17-d6-reconciliation.md`.
- Prior admin contract and destination review: `docs/development/monad-0.1-alpha-m19-admin-reconciliation.md`.
- Macroqualification review: `docs/development/monad-0.1-alpha-m24-macroqualification-review.md`.
- Prior recovery readiness: `docs/development/monad-0.1-alpha-m26-recovery-readiness.md:64-153`.
