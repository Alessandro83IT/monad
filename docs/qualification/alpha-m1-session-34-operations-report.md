# Alpha M1 Session 34 — Operations report

## Scope and deliverables

This task prepared [the Session 34 administrative runbook](alpha-m1-session-34-admin-runbook.md) only. It consulted all requested Session 33 reports and the post-fix reconciliation manifest, the authorization policy, and the current installer draft. The new runbook provides separate promotion and rollback checklists, explicit operator states, evidence capture, stop conditions, VM-side prerequisites, and post-recovery re-verification.

No installer, test, pin, manifest, security report, installed path, VM state, fixture, D6 residual, receipt, log, or backup was changed. No `sudo`, installation, canary, or privileged test was run. This agent did not directly inspect the VM or D6 paths.

## Transaction contract coordinated with Core

Core communicated the following candidate interface and semantics during this documentation work; they must be confirmed against the final installer and hash before the runbook is executed:

- Promotion: `install-athena-alpha-m1-admin.sh install ROOT_BACKUP_DIR`.
- Rollback: `install-athena-alpha-m1-admin.sh rollback ROOT_BACKUP_DIR --transaction-id ID --approval-file ABSOLUTE_PATH --approval-sha256 SHA256`; `ID` must match the backup directory basename and `SHA256` must match the approval file bytes.
- The backup directory basename is the transaction ID. A persistent lock serializes operations.
- `backup-manifest.json` and canonical sorted compact JSON `transaction.json` (schema 1, newline terminated) bind transaction ID, backup manifest digest, ordered destination paths, original presence/digests/metadata, and ordered candidate digests.
- Exclusive, synced markers are named `prepared`, `install-started`, `install-partial`, `install-complete`, `rollback-started`, `rollback-partial`, and `rollback-complete`. All marker contents are `transaction=ID` except `install-partial`, which contains `transaction=ID exit=RC`.
- Signals/EXIT do not restore targets. After `install-started`, failures preserve transaction evidence; SIGKILL/power loss may leave only the start marker. A later install refuses an incomplete transaction.
- Explicit rollback approval is canonical unsigned JSON, root-owned mode 0600 in administrative mode, descriptor-relative no-follow path checked, with exactly schema 1, action `rollback`, transaction ID, backup-manifest SHA-256, and transaction-record SHA-256. Candidate rejects hardlinks and ACLs and requires the CLI SHA-256 of the approval file bytes. A separate external human approval record is required because the token is unsigned; its digest binds the file but does not prove approver identity.
- The exclusive `rollback-started` marker makes rollback one-use. If rollback becomes partial, the command refuses another rollback attempt; manual recovery and a new approval are required.
- Validation must complete for approval, transaction records, and all destinations before the first mutation. Multiple file renames remain non-atomic.

The interface/record details were checked against the current shared installer source while it was still being edited. Core reports lock parent validation and exclusive, non-truncating lock creation followed by descriptor owner/mode/link-count checks. The transaction installer candidate hash reported by Core is `157f080c4d5692af9a6f3ea8ad8b778165bd5a60c4a13b84b4cef4f0aea2c770`. Security's final disposition is **BLOCKED** pending reviewed application and re-review of the pin-only patch. The exact pin-updated installer content hash reported by the coordinator is `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7`; that content has not received Security approval. The runbook and this report remain drafts, not approval to promote or rollback.

## Evidence scopes and current state

- **Session-supplied VM evidence (not recollected here):** 15 failed units with `MainPID=0` and empty `ControlGroup`; installed/candidate runner hashes; missing installed gateway support/canary; root-owned mode-0700 fixture and `/var/tmp/athena-d67-ext4`; D6 residual `athena-d6-8yRDjc` and named contents; protected build with 18 privileged cases; prior canary hash/pin mismatch. Empty control groups do not prove descendant absence. Preserve the D6 residual in place.
- **Session 33 repository findings:** the existing installer invokes `rollback_all` automatically for some post-marker errors. Session 33 also described individually atomic target renames and a receipt ancestor-symlink gap. The old pin-only installer hash is not usable as a candidate.
- **Operations direct observations:** workspace was `/home/alessandro/Projects/monad`, branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`; many modified/untracked sources, reports, builds and evidence existed. They were preserved. No direct VM observation was made.
- **Session 34 test state:** Operations ran no tests. Verification reports **PASS 25, FAIL 0, SKIP 0, TIMEOUT 0** for non-privileged transaction, receipt-path and admin static tests, plus `bash -n`, against installer SHA-256 `157f080c4d5692af9a6f3ea8ad8b778165bd5a60c4a13b84b4cef4f0aea2c770`; see [Verification report](alpha-m1-session-34-verification-report.md). These tests do not establish actual host power-loss durability, VM ownership/ACL behavior or systemd/cgroup behavior. CTest, VM/admin mode, canary and all 18 privileged cases are NOT RUN. Security owns independent final review. Coordinator owns final manifest, hashes, integration and decision.

## Recovery and qualification disposition

No transaction recovery or rollback was performed; no `sudo`, install, live canary, privileged test, pin update, or VM mutation was performed by Operations; and no administrative state has been established as recovered. No Alpha M1 qualification claim is made. Despite the non-privileged test PASS, the final disposition remains **BLOCKED** by Security's pending review of the pin-only patch and its resulting installer hash, as well as VM-side prerequisites: VM/snapshot identity, descendant/cgroup state, D6/fixture/mount preservation, protected-build provenance, staged hashes and receipt. The runbook requires separate approval for promotion and rollback and a new VM verification pass before campaign resumption.
