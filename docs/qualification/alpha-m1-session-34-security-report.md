# Alpha M1 Session 34 — independent security review

**Disposition: BLOCKED for administrative promotion.** The rollback-transaction changes reviewed below close the Session 33 automatic-rollback defect in the exact installer hash. The current installer still pins the Session 33 runner, support, and canary hashes, so it rejects the corrected artifacts now in the checkout. The proposed pin-only content hash was checked separately, but has not been applied or reviewed as a final candidate file. No administrative change, installation, live canary, privileged test, or VM inspection was performed.

This report is a static review and independent assessment of the supplied non-privileged test results. It is not approval to install or execute privileged changes. VM behavior, the approved artifact transfer, and external human authorization remain separate gates.

## Exact identities reviewed

These hashes identify files in the shared container checkout, not files observed on the VM:

| Artifact | SHA-256 |
|---|---|
| Installer candidate reviewed for transaction safety | `157f080c4d5692af9a6f3ea8ad8b778165bd5a60c4a13b84b4cef4f0aea2c770` |
| Canary with receipt directory descriptor walk | `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` |
| Runner | `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` |
| Gateway support | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` |
| Sudoers draft | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` |
| Transaction regression suite | `8c00ec74ae51be70eec1c07bad3357c46e1908c39defee117f73def7a00b37da` |

The installer currently pins runner `2290b78c55c72489c0e7dc3bb1c1fedbc573e6bc605e690b6afd3a8ab326b24c`, support `86be38660154161b6f71063567241fed3fbe1ef270d04a73b7c00a69126a9d28`, and canary `0c2567320d3f5c461d0c9a4a19beaf62b83615c511c28e699810bbdfa49f3107`. Those values do not match the three corrected artifacts above.

I independently reconstructed the proposed pin-only content in memory, replacing exactly those three literals with the checkout hashes above. It produces SHA-256 `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7`, matching the coordinator-supplied proposal. No source file or approved pin was changed. This verifies the diff arithmetic and artifact identities only; it is not a review of an applied candidate, manifest, or installer hash.

## Transaction safety findings

- The install EXIT trap no longer calls `rollback_all`. After `install-started`, ordinary errors record `install-partial` where possible, preserve the backup, transaction record, marker, and remaining staged files, then exit. A failure before the marker removes only temporary staging files and does not change administrative targets.
- Runner, support, and sudoers are published by separate same-filesystem renames, with parent-directory `fsync` calls. They are individually atomic; the three-file update is not one atomic transaction. The durable start marker precedes publication. An interruption can leave a mixed generation; the code does not label it complete and blocks a later install while the transaction is incomplete.
- A fresh transaction path is required. A persistent lock serializes installer invocations, and the backup-root scan rejects incomplete transactions. The rootless test mode requires a non-root caller and redirects the administrative paths beneath a private fixture.
- The transaction record binds the transaction directory identity, backup-manifest digest, exact destination list, recorded originals, and candidate digests. It is written exclusively and synced. Before recording, current target content and metadata are checked against the captured backup. Rollback checks the exact schema and destination inventory, transaction ID, manifest and transaction hashes, approval-file digest, approval fields, and every destination's recorded content generation.
- Rollback is a separate action requiring the matching transaction ID, canonical approval JSON, and a separately supplied SHA-256 of that exact approval file. A denial occurs before `rollback-started`; tests verify backup, receipt, and targets remain unchanged. After rollback begins, a failure stops further restores, records partial state where possible, and prevents an automatic retry. The restore renames remain individually atomic, not a single atomic rollback.
- Receipt handling in the canary and installer walks the absolute receipt path with directory descriptors and `O_NOFOLLOW`, then opens the receipt relative to the final descriptor. Receipt publication archives existing evidence before replacement and syncs the containing directory. No rollback path deletes or rewrites the receipt.
- Backup and transaction files are checked for regular-file type, ownership/mode, link count, ACLs, inventory, and hashes. The supplied D6 residual is treated as preservation evidence; this review did not inspect or modify it.

## Verification evidence

Verification reports `bash -n` PASS and **25 PASS, 0 FAIL, 0 SKIP, 0 TIMEOUT** across static, transaction, and receipt-path tests against the exact installer hash `157f080c…c770`. Reported cases include failures before and after the marker, SIGINT/SIGTERM, SIGKILL fault injection, failures between renames, incomplete/reused transactions, concurrent invocations, approved rollback, rollback refusal without mutation, tampered destinations, symlink/hardlink approval files, malformed transaction arrays, and partial rollback stopping without retry. These were non-privileged isolated fixtures; they do not exercise the Debian VM or privileged target directories.

SIGKILL was injected at deterministic process fault points. No actual power-loss or filesystem crash campaign was performed. Static marker and `fsync` handling avoids recording completion before destination verification, but durability and recovery after real VM power loss remain **unverified on the target filesystem**. A partial transaction requires manual recovery; it is not automatically recoverable or considered successful.

## Authorization and remaining limits

The approval JSON is unsigned. The CLI digest binds the bytes to an independently recorded approval artifact, but neither the digest nor JSON proves approver identity. The runbook correctly requires a distinct human administrator decision and an external audit record containing approver, scope, transaction ID, manifest/transaction hashes, and approval-file hash. The installer cannot prevent a privileged root operator from fabricating an unsigned token or bypassing the script with direct root operations; this mechanism relies on the human approval and root-administration boundary. It is not protection against compromised root.

The pin-only proposal must be applied only through the explicitly reviewed promotion workflow. Then recompute and review the final installer hash and candidate manifest before any approval or transfer. Until that happens, the exact installer reviewed here is incompatible with the corrected staged artifact hashes and the candidate remains blocked.

The VM prerequisites are still not verified by this review: VM/snapshot/boot identity, installed hashes, systemd/cgroup behavior, absence of descendants of the 15 failed units, fixture and mount state, sudoers validation, receipt production/freshness, protected-build provenance, and the 18-case inventory must be recollected on the VM. The supplied D6 residual and fixture observations remain user-provided evidence, not container observations. Preserve the D6 residual, fixture, receipts, backups, markers, and logs; do not infer Alpha M1 qualification from this review.
