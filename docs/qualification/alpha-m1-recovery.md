# Alpha M1 recovery and activation

## Current status

The working tree already contains qualification fixes and retained evidence. Do not clean or regenerate these in place. The current environment is not the qualified Debian VM. No root operation, gateway installation, sudoers update, canary runtime, or privileged Alpha M1 test is authorized by this local implementation task. Alpha M1 remains **INCOMPLETE** until the approved VM campaign completes.

The existing canonical staging workflow is `alpha-m1-admin-staging.md`; the older session report may describe superseded commands. Review and reconcile every source hash against the current installer before staging. Historical reports have contained different runner/canary/installer digests. A mismatch is a stop condition, not an invitation to update pins automatically.

## Backup and rollback

1. In the disposable Debian qualification VM, record VM identity, boot ID, filesystem/mount state, package/tool versions, and a snapshot identifier. Keep the original evidence and D6 residual unchanged.
2. Before any approved administrative update, use a new, unique, root-owned `0700` backup directory. Capture original files and their type, owner, group, mode, ACL status, and SHA-256 in a manifest. Fsync the manifest and backup directory; independently verify all hashes before proceeding.
3. Stage candidate files in a protected root-owned directory. Compare their hashes with a separately reviewed manifest and inspect source diffs. Never copy directly from a writable checkout into a privileged path.
4. Run the reviewed installer only with explicit human authorization. Validate sudoers with `visudo -c`; retain the backup and transaction output.
5. If a failure occurs, stop further privileged work. Preserve logs, receipt, fixture, backup, and partial-state report. An administrator reviews the backup manifest and selects a state-changing rollback explicitly. Verify restored hashes and metadata. Do not automatically delete the backup or evidence.

The reviewed installer verifies its aggregate backup manifest and all destination metadata before rollback, fsyncs backup entries/manifests and per-file restore renames, and retains the backup. Runner/support/sudoers promotion remains a sequence of per-file renames rather than one atomic transaction; power loss or SIGKILL between them can leave a partial installation. Treat that as a manual recovery event and never remove the backup.

## Debian VM activation sequence

After independent security review and explicit administrative approval:

1. Reconcile current hashes for runner, canary, sudoers draft, installer, and all pinned build artifacts. Confirm the protected build is the authenticated build intended for Alpha M1.
2. Verify required systemd, `visudo`, ACL tooling, ext4 fixture filesystem, root-owned paths, and absence of unsafe symlinks or writable parents.
3. Preserve/create the approved empty ext4 fixture; do not format or mount devices through this workflow.
4. Run the reviewed systemd canary and capture a fresh receipt bound to artifact hashes, boot ID, systemd version, and expiration.
5. Install only the reviewed pinned artifacts, using the new verified backup directory; run `visudo -c` and inspect installed ownership/modes.
6. Run the non-root orchestrator, which must verify the CTest inventory before invoking only the 18 exact approved cases through the runner.
7. Review each case result, root log and output digest, complete user report, unit cleanup, mount state, and fixture state. PASS requires all expected cases PASS and all preservation checks satisfied. Any mismatch, SKIP, FAIL, TIMEOUT, incomplete log, non-empty fixture, or unconfirmed unit stop leaves qualification incomplete.

The independent static security review has closed the reported TOCTOU, timeout-classification, client/unit-termination, aggregate-backup, and gid-restoration findings. This review does not prove systemd/cgroup runtime behavior or qualify Alpha M1. Install or activate only after the separate administrative review approves the exact hashes and the VM prerequisites. Never delete the D6 residual, logs, receipts, or backups as part of recovery.
