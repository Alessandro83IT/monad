# Alpha M1 Session 34: administrative transaction runbook

**Status: administrative review draft. Not authorization to execute.** Applies only to the separately identified disposable Debian qualification VM after the final candidate and this procedure receive explicit review. No step authorizes any state-changing action by itself. Do not use it on the development container. Preserve the D6 residual and all receipts, backups, markers, logs, and reports in place.

**Session 35 disposition: BLOCKED pending VM-side evidence and separate human gates.** Session 34B closed the installer pin reconciliation: the exact installer and manifest identities below were independently reviewed by Security, and the Session 34B closure report records all four pins matching the candidate files. This closes the previously reported pin mismatch and pending-review items. It does not authorize staging, canary, installation, privileged tests, or rollback. The baseline candidate identities are installer `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7`, manifest `00fd4036bf0f3873f5c2a0e932c912b4709460fa52c35a1c2564016a92811262`, runner `c68bca67a7a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def`, gateway support `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514`, and canary `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2`. These are repository candidate identities, not VM transfer or installed-file evidence. No hash may be substituted without a new explicit review.

The Session 34B closure report records `bash -n` PASS and final static/regression result **25 PASS, 0 FAIL, 0 SKIP, 0 TIMEOUT**. It also records that all 18 privileged cases, live canary, installation, VM receipt, VM ownership/ACL, systemd/cgroup, and real power-loss behavior were NOT RUN. Preserve those limits; this runbook does not claim Alpha M1 qualification.

## Operator contract and transaction states

The candidate interface reported by Core is:

```text
install-athena-alpha-m1-admin.sh install ROOT_BACKUP_DIR
install-athena-alpha-m1-admin.sh rollback ROOT_BACKUP_DIR --transaction-id ID --approval-file ABSOLUTE_PATH --approval-sha256 SHA256
```

This is the interface Core reported from the current candidate source; the coordinator must confirm the final reviewed hash and rerun regression tests before use. `ROOT_BACKUP_DIR` is a fresh, unique direct child of `/var/lib/athena-alpha-m1/backups`; its basename is the transaction ID (`txn_id`) and the `--transaction-id ID` argument must equal it exactly. Never reuse one. A persistent lock file at `/var/lib/athena-alpha-m1/admin-install.lock` is protected by `flock`; the lock is released when the process exits. Do not remove or replace the file to clear a busy condition; investigate the holder and transaction state.

The transaction records are `backup-manifest.json` and canonical sorted compact JSON `transaction.json` (schema 1, newline terminated). It binds `transaction_id`, `backup_manifest_sha256`, `destinations` (runner/support/sudoers absolute paths in order), `originals` (per-target original presence and, when present, SHA-256, uid, gid, mode), and `candidates` (SHA-256 values in destination order). The approval JSON is also canonical sorted compact JSON, newline terminated, with exactly `schema: 1`, `action: "rollback"`, `transaction_id`, `backup_manifest_sha256`, and `transaction_sha256`. The rollback CLI additionally requires `--approval-sha256 SHA256`, equal to the SHA-256 of the exact approval-file bytes. Candidate validation rejects approval hardlinks and ACLs. Markers are exclusive and synced; payload is `transaction=ID` for `prepared`, `install-started`, `install-complete`, `rollback-started`, `rollback-partial`, and `rollback-complete`; `install-partial` records `transaction=ID exit=RC`. Core's current source uses these names and payloads; independently confirm the final reviewed source hash and tests before use.

Interpret markers conservatively:

| Observation | Operator state and action |
|---|---|
| No transaction directory | No transaction is established; inspect independently before considering a new one. |
| `prepared`, no `install-started` | Preparation only if manifest, transaction record and all destinations validate. Any discrepancy: stop and preserve. |
| `install-started` without `install-complete` or `rollback-complete` | Incomplete/indeterminate transaction, including SIGKILL or power loss. Do not rerun install. Capture state and request recovery review. |
| `install-partial` | Partial install explicitly recorded (marker payload records the exiting status). Preserve every artifact and destination; inspect all files, do not auto-retry. |
| `install-complete` | Candidate install reported complete; independently verify all destination hashes and metadata before the next gate. |
| `rollback-started` without `rollback-complete` | Incomplete/indeterminate rollback. Preserve evidence and request manual recovery review. |
| `rollback-partial` | Partial rollback. No automatic retry; inspect all destinations against transaction record and backup. |
| `rollback-complete` | Rollback reported complete; independently verify all destinations and metadata. Keep backup and receipts. |
| Conflicting, missing, malformed, or unexpected markers | Stop. Do not infer success or repair markers by hand. |

The three installed-file renames are individually atomic, not one atomic transaction. SIGINT, SIGTERM, EXIT, SIGKILL, or power loss must not trigger implicit target restoration. If interrupted after `install-started`, expect mixed destination generations; keep the transaction directory intact. A later install must refuse any incomplete transaction rather than reuse it.

Rollback requires a distinct, recorded human administrative decision. Candidate validation requires the canonical JSON fields above, a regular mode-0600 file owned by the caller (root in administrative mode), descriptor-relative no-follow traversal of every path component, one link, and no ACL; the caller must also supply the exact SHA-256 of the approval file bytes. Obtain approval before creating this file; record approver, UTC time, VM/snapshot identity, transaction ID, backup-manifest hash, transaction-record hash, approval-file SHA-256, and scope in an independent audit record. The token is unsigned: its supplied digest binds the exact file recorded by the external decision, but is not a cryptographic signature or proof of the approver's identity. The explicit `rollback-started` marker makes the operation one-use: any prior rollback attempt blocks another automatic attempt, including after partial failure; recovery then requires manual handling and fresh separate review. A refusal before this marker must leave receipt, backup, markers, and destinations byte-for-byte unchanged.

## Stop conditions

Stop without further mutation on identity/hash mismatch, symlink or ownership/mode/ACL anomaly, unknown file or process, uncertain systemd/cgroup state, mount/fixture mismatch, stale or invalid receipt, lock contention, incomplete or inconsistent transaction, insufficient storage, timeout, interruption, or any validation failure. Preserve stdout/stderr and capture current state read-only. Do not clean, move, rename, chmod, chown, remount, or overwrite D6, fixture contents, transaction directories, backups, markers, or receipt archives. Do not infer that descendants exited from `MainPID=0`, unit `failed`, or a missing cgroup file.

## Independent approval gates

Each gate below requires a new, explicit decision naming its exact scope and evidence. A prior gate, report, operator action, or approval does not imply any later gate. Read-only verification is an evidence collection/review gate and does not authorize state changes.

1. **Administrative read-only verification:** approve only collection and review of VM identity, processes/cgroups, units/logs, filesystems, fixture/D6 metadata, build provenance, artifact hashes, and test inventory. No state-changing command is authorized.
2. **Staging approval:** separately authorize creation of a named staging tree and transfer of the exact reviewed candidate files after gate 1 passes. Staging approval does not authorize executing the canary or installer.
3. **Real canary approval:** separately authorize the exact reviewed canary, VM/snapshot/boot identity, and receipt destination. This is privileged execution and is not covered by staging approval.
4. **Installation approval:** separately authorize the exact installer and artifact hashes, destinations, VM/snapshot/boot identity, and unique backup path. A canary receipt does not authorize installation.
5. **Privileged campaign approval:** separately authorize the exact protected build identity and named 18 privileged cases after all preceding evidence and install review pass. Installation does not authorize test execution.
6. **Rollback approval:** separately authorize rollback for one named transaction using the transaction/backup/approval hashes and exact VM identity. No earlier approval authorizes rollback; rollback does not authorize resuming tests.

## Promotion checklist (each box requires an evidence reference)

### A. Prepare and approve exact artifacts

- [ ] Identify approved VM by machine ID, Debian release, hypervisor, snapshot/disposable-state ID, boot ID, systemd version, and UTC timestamp. Preserve capture outside the checkout in a protected evidence location.
- [ ] Verify final manifest, source revision and inputs, candidate artifact hashes, installer hash, sudoers draft hash, unit policy, protected build manifest, and exact 18-case inventory. Independently hash transferred files on the VM. Container hashes do not establish VM file or build identity.
- [ ] Confirm the final installer has no rollback in EXIT/signal/error traps; explicit rollback requires the reviewed approval file; incomplete transactions block later installs; lock behavior and all marker transitions match this runbook. Obtain Security approval of exact final hashes and implementation.
- [ ] Record distinct human decisions for staging, canary/receipt execution, installation, and later privileged campaign. Approval for one action does not authorize another. For install, record `txn_id` (backup directory basename), exact destination paths, candidate and current hashes, VM/snapshot/boot identity, and selected unique backup path.
- [ ] Recollect the systemd inventory for all `athena-alpha-m1-*.service` units. Enumerate process descendants and cgroups independently; establish termination or stop for operator investigation. Do not kill or restart units as an automated continuation.

### B. Verify VM and evidence prerequisites

- [ ] Capture systemd unit properties, cgroup v2 membership/events where available, `/proc` PID/PPID/command/cgroup observations, and system logs. Preserve raw outputs and UTC collection times.
- [ ] Inspect fixture, mount table, ACLs, owners/modes and filesystem identity read-only. Confirm no unexpected content. Record names and metadata for `/var/lib/athena-alpha-m1/ext4-fixture`, `/var/tmp/athena-d67-ext4`, and D6 residual `athena-d6-8yRDjc`.
- [ ] Preserve D6 in place: `generations`, `projection`, `store`, and `generations/.athena-locks/state.lock` are evidence. Do not delete, move, rename, chmod, chown, or mount over them. Any acquisition copy needs a separately reviewed read-only method and must retain originals.
- [ ] Inventory installed runner/support/canary/sudoers and all transaction/backup directories. Hash and record metadata. Reconcile the protected build binaries and the exact 18 privileged test names without running them. If source-to-build provenance cannot be established, do not authorize campaign execution.
- [ ] Verify backup parent, log parent, staging parent and every path component are root-owned as required, mode/ACL compliant and symlink-free. Check free space and VM identity again immediately before mutation.

### C. Stage and promote

- [ ] Create a fresh, unique, empty root-owned mode-0700 staging directory. Stage only reviewed artifacts and supporting manifest/reports. Never reuse or overwrite an unknown staging tree.
- [ ] Verify exact inventory, regular-file types, owners, modes, ACLs, hashes and symlink-free path components on the VM. Validate sudoers draft using `/usr/sbin/visudo -cf STAGED_DRAFT`; inspect its exact allowlist and systemd unit policy.
- [ ] Obtain separate explicit approval naming the exact installer digest, candidate digests, destination list, VM/snapshot/boot ID, and unique backup directory. Require the backup directory not to exist and confirm no incomplete transaction exists.
- [ ] If a canary receipt is separately approved, run the reviewed canary only once. Check receipt schema, candidate hashes, boot ID, systemd version, freshness, root ownership, mode, ACL, path safety, and receipt archive. On any uncertainty stop; do not retry or install.
- [ ] Run only the reviewed installer `install` action with the approved unique backup path. Capture complete output. Do not improvise arguments or edit installed paths manually.
- [ ] Verify `transaction.json`, backup manifest, transaction ID, and markers. Verify every destination hash, owner, group, mode, ACL, and symlink state independently; run `visudo -c`. Keep the backup and transaction directory immutable for subsequent review.
- [ ] A successful command is not sufficient: a second operator reviews evidence and reconciles all files/markers before declaring install complete. Do not launch the 18-case campaign until it receives a separate approval and all protected-build, receipt, process and VM gates pass.

### D. Error and incomplete transaction response

- [ ] On nonzero exit, SIGINT/SIGTERM, timeout, `install-partial`, or uncertain output, do not rerun install and do not invoke rollback as a reaction. Preserve staging, backup, marker, receipt/archive, stdout/stderr and system logs.
- [ ] On SIGKILL or power loss, first re-identify VM/snapshot/boot. Read the lock, transaction records, markers and three destinations without changing them. `install-started` without terminal completion is incomplete even if targets appear to match candidate hashes.
- [ ] Confirm process/cgroup state for any systemd units independently. If the lock is held or process state is uncertain, stop; do not delete the lock or terminate a process automatically.
- [ ] File an incident/recovery record with transaction ID, manifest and transaction hashes, markers, per-destination hashes/metadata, relevant logs and receipt state. Keep D6 unchanged. Request new human review before any state-changing recovery.

## Rollback checklist (separate approval required)

- [ ] Obtain a separate administrator decision explicitly authorizing rollback of this transaction. Record approver, decision time, reason, VM/snapshot/boot ID, transaction ID, destination list, backup-manifest SHA-256 and transaction SHA-256 in the independent audit record.
- [ ] Independently validate that the backup directory is the recorded transaction, root-owned mode 0700, ACL-free and symlink-free; verify every manifest entry, payload digest, owner/group/mode/type, original presence marker, transaction record and all marker files. Preserve originals.
- [ ] Re-hash all three destinations and require each to match an allowed recorded original or candidate state. Any unknown or mixed/unexpected file, symlink, metadata drift or invalid approval artifact blocks rollback before the first mutation.
- [ ] Create the root-owned mode-0600 approval JSON only after the separate approval. Bind exact transaction ID, backup-manifest hash and transaction-record hash. Compute SHA-256 of the exact approval bytes independently and record it in the external approval record. Never reuse an approval file for another transaction.
- [ ] Run only the final reviewed command `install-athena-alpha-m1-admin.sh rollback ROOT_BACKUP_DIR --transaction-id ID --approval-file ABSOLUTE_PATH --approval-sha256 SHA256`, under the approved VM session. `ID` must exactly match the backup directory basename; `SHA256` must match the approval file. Capture output. The installer must revalidate approval, identity, records and every destination before mutating any target.
- [ ] On failure, `rollback-partial`, interruption, SIGKILL, power loss, or missing `rollback-complete`, stop and do not retry automatically. Preserve all files and seek manual recovery review. Multiple restore renames are not atomic.
- [ ] Independently verify all three destinations, `visudo -c`, markers, hashes, owners/modes/ACLs, systemd state, lock status and receipt/archive. Preserve approval artifact, backup, transaction manifest, markers, logs and receipt without cleanup.
- [ ] Before resuming qualification, re-identify VM/snapshot/boot, reacquire process/cgroup and fixture/mount/D6 evidence, reconcile installed hashes/build/receipt, and obtain a new explicit campaign approval. Rollback completion does not qualify the build or authorize tests.

## Current evidence limits and remaining prerequisites

Session 33's 15 failed-unit observations (`MainPID=0`, empty `ControlGroup`) do not establish descendant absence. The fixture and D6 metadata and protected-build inventory were session-supplied VM observations and require recollection. The D6 residual remains preservation evidence; its current VM state has not been inspected by this runbook author.

Session 34/34B reviewed and corrected the automatic rollback behavior and receipt-path traversal; the exact Session 34B installer/manifest identities above passed the recorded review and pin reconciliation. Automatic rollback is no longer a pending defect for that exact candidate. The three target renames remain individually atomic, not one atomic transaction. Non-privileged tests do not establish actual VM behavior or durability after power loss. Still-open VM-side gates include approved VM/snapshot identity, exact transferred and installed hashes, proven protected-build provenance, receipt generation and security, systemd/cgroup/process residual state, fixture/mount/D6 metadata, root ownership/ACL behavior, sudoers validation on the target, and the separately approved canary/install/campaign actions.
