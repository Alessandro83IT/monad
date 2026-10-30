# Alpha M1 — Session 40 diagnostic evidence acquisition procedure

**Prepared for a later explicit operator decision. This document does not authorize execution.** It covers only the non-root read-only collector. It does not authorize staging, a canary, installation, privileged tests, rollback, cleanup, or any VM/system mutation.

## Preconditions to submit for approval

The operator's approval record must identify all of the following before any collector run:

1. VirtualBox host identity, exact VM UUID/name, current snapshot UUID/name, operator, time window, and the host-side read-only commands. The VM must not be started/stopped or have its snapshot changed by this procedure.
2. Guest machine-id and boot-id observations that bind the running guest to the host VM/snapshot. `systemd-detect-virt` alone is insufficient.
3. Reviewed checkout path, branch/HEAD, final runner SHA-256 `468756ff8cc1f55e09e4560ec1e47974b53a076219715fa35bf36874fb2cc618`, collector SHA-256 `6527d34cbfcca75244a77a1457f13e3d8e1b79aee5393f348c73bc96d74a74d2`, candidate directory, and an operator decision that explicitly approves these exact bytes. Do not assume Session 39's runner SHA applies after the Session 40 audit correction.
4. Review and approve the collector's **complete fixed read set** as documented in Sessions 37/38: process arguments, systemd inventories/properties and journals, protected path metadata, D6 names-only inventory, the collector's receipt hash where readable, build caches/generated files/artifact hashes, and the generated CTest inventory helper inputs. The collector does not provide per-check selective skipping. Exact receipt/backup/log content hashes and root-only protected reads need the separate narrow approvals specified in the earlier procedures; do not imply broader access from approval of ordinary-user collection.
5. A pre-existing canonical private evidence parent owned by the ordinary VM user and not group/world writable, plus a separate pre-existing private capture directory owned by that user with mode 0700. Both must be named exactly in the approval and must not be inside the checkout, historical evidence, fixture, D6 residual, or administrative paths. No existing capture names may be overwritten.
6. Review of the read-only procedure, output retention/transfer method, and handling of incomplete/partial records.

If the Session 40 tree verifier cannot reproduce 149 files and digest `f229f1b367f31053a283f564b3d2fa3edd713bfa1ede5134c899466f1bb8f67f`, stop. Preserve the historical corpus unchanged and record BLOCKED.

## Approved sequence

1. On the approved host, record VM and snapshot identity. On the guest, record machine-id/boot-id and reconcile them before other collection.
2. In the reviewed checkout, verify the approved runner and collector SHA-256. The collector must equal the fixed digest above. If runner bytes changed from the approved hash or collector differs, stop.
3. Run the runner without arguments. It must print the fixed collector digest and `verification_only=true`. This pass opens/hashes/seals bytes only; it is not VM identity proof or execution approval.
4. Only after the recorded human authorization, run as the ordinary guest user. Use the already approved existing evidence parent. The collector itself creates one unique new child and refuses collisions.
5. Capture stdout, stderr, and shell wait status in new no-clobber paths in the separate approved capture directory. The following is a command shape only; replace placeholders only with exact approved paths. Reserve all output names before launch and do not use `set -e` so the wait status can be captured:

   ```bash
   set -o noclobber
   exec 3> /approved/capture/runner.exit-status
   python3 -I /reviewed/checkout/qualification/alpha-m1-verified-evidence-runner.py \
     --execute --output-parent /approved/existing/evidence-parent \
     --candidate-dir /reviewed/checkout/qualification \
     > /approved/capture/runner.stdout 2> /approved/capture/runner.stderr
   rc=$?
   printf '%s\n' "$rc" >&3
   exec 3>&-
   ```

   A status from shell redirection failure does not prove the runner started. Preserve partial/ambiguous output and classify it BLOCKED. The JSON runner record is emitted after collector exit; it is not tamper-resistant or durable against power loss, SIGKILL, interpreter failure, or capture failure. Preserve the supervising shell status as a separate observation.
6. Review every collector output, raw file, check classification, and generated inventory report. Retain PASS/FAIL/BLOCKED/TIMEOUT/NOT RUN exactly as observed. Hash the completed output tree with a separately reviewed read-only no-follow procedure. Keep D6 and the historical evidence tree unchanged.
7. For protected paths, follow Session 37/38 sequential review: inspect path components first, review, then approve each next exact command. Stop on symlinks, unexpected owner/mode/ACL/mount, access errors, or identity disagreement. Do not use `sudo` or a general root shell.

The collector and this process do not run CTest tests, canaries, installers, rollback, cleanup, or system mutations. `--show-only` is not part of this approved sequence.

## Disposition after collection

Collection is diagnostic evidence only. Review the VM/snapshot binding, every protected read, process/cgroup reconciliation, D6 preservation, receipt/backups/log evidence, exact generated inventories, and build provenance independently. Build provenance is currently partial for local builds and unverified for the two expected VM paths. That gap need not prevent a narrowly approved diagnostic capture, but it continues to block any final qualification decision based on those binaries. Separate later approvals are required for staging, canary, installation, privileged campaign, rollback, and cleanup.
