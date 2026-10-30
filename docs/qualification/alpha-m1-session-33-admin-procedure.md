# Alpha M1 Session 33: administrative procedure

Status: **draft for explicit administrative review; not executed**. This runbook is for the identified Debian qualification VM only. It does not authorize installation, cleanup, rollback, or privileged test execution by itself. Keep D6 evidence in place and preserve all output and backups.

## Approval gates and stop rule

Require recorded approval from the VM owner for (a) target VM and snapshot, (b) exact candidate hashes, (c) running the root canary, (d) installation and its unique backup path, (e) the privileged campaign, and (f) any rollback. An approval for one gate does not approve later gates. Record approver, UTC time, VM machine ID, boot ID, snapshot ID, artifact digests, and decision in an external evidence location.

At any mismatch, unreadable path, unrecognized process, unexpected unit/cgroup state, invalid receipt, incomplete evidence, timeout, interruption, or partial install/rollback: stop. Do not retry cleanup or infer termination from `MainPID=0`, `failed`, or missing `cgroup.events`. Preserve the fixture, all markers, D6 paths, logs, receipts, and backups for manual review.

## 1. Identify and baseline the VM

Use the existing read-only collector from an interactive shell on the target VM. It does not invoke sudo; save its complete stdout/stderr outside the checkout:

```sh
/path/to/reviewed/qualification/alpha-m1-vm-preflight.sh
```

Separately record `id`, `/etc/os-release`, `systemd-detect-virt`, machine-id, boot-id, systemd version/state, hypervisor snapshot/disposable-state identity, and UTC time. Confirm this is the approved Debian VM, not the development container. Inventory the protected build with `ctest --show-only=json-v1 -L privileged`; do not use `ctest -N` because CTest may write its temporary log. Require the exact 18 expected names, exact artifact digests and expected owner/mode. Do not launch tests in this phase.

## 2. Establish no residual processes and preserve D6

Capture `systemctl list-units 'athena-alpha-m1-*.service' --all`, and for every matching unit capture `systemctl show` fields `LoadState,ActiveState,SubState,MainPID,ControlGroup,Result,ExecMainCode,ExecMainStatus`. Capture cgroup v2 `cgroup.events` where the unit's control group exists, and enumerate processes with their PID, PPID, command, and cgroup from `/proc`. Correlate systemd, cgroup, and process results; if a control group is missing or empty, independently establish that no descendants remain. Any ambiguity is a stop requiring operator investigation; never kill processes as an automated continuation.

The reported 15 failed units with `MainPID=0` and empty `ControlGroup` do not establish that descendants are gone. Do not remove or restart units to make the inventory look clean.

Record metadata and a names-only inventory for `/var/lib/athena-alpha-m1/ext4-fixture` and `/var/tmp/athena-d67-ext4`, including the D6 residual `athena-d6-8yRDjc` and its `generations`, `projection`, `store`, and `.athena-locks/state.lock`. Do not traverse file contents unnecessarily; do not delete, rename, move, chmod, chown, mount over, or “clean” these paths. Preserve D6 in place as primary evidence. If an approved evidence snapshot is needed, use a separately reviewed read-only acquisition method and preserve originals.

## 3. Verify filesystem, fixture, and existing installation

Capture `stat`, ACLs, `findmnt -T` and mount options for `/var/lib/athena-alpha-m1`, the ext4 fixture, `/var/tmp/athena-d67-ext4`, `/opt/athena-qualification/build`, runner, support module, canary, and sudoers file. The fixture must be the expected ext4 filesystem, root-owned mode 0700, ACL-free, and in the exact state accepted by the canary/installer. A nonempty fixture is a stop; do not empty it. Confirm backup and log parents are root-owned mode 0700 and ACL-free. Hash installed runner/support/sudoers and record current gateway/canary presence and metadata; absent support or canary is an observation to record, not a reason to improvise a copy.

The VM observations supplied to Session 33 are prior interactive evidence, not observations by this container. Recollect them and compare before any change.

## 4. Review, approve, and stage exact artifacts

Before staging, independently review the proposed source changes, installer, sudoers draft, unit policy, build manifest, reports, and Security review. Approve exact digests from the final candidate manifest. Current container candidate hashes are runner `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def`, gateway support `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514`, and canary `caeb095d1e9a5f3bb631a415f49f0f039ab36874352cba43a86c0a6207440893`. The installer still pins the previous runner `2290b78c55c72489c0e7dc3bb1c1fedbc573e6bc605e690b6afd3a8ab326b24c`, support `86be38660154161b6f71063567241fed3fbe1ef270d04a73b7c00a69126a9d28`, and canary `0c2567320d3f5c461d0c9a4a19beaf62b83615c511c28e699810bbdfa49f3107`. The post-fix reconciliation is `qualification/alpha-m1-build-reconciliation-session33-postfix.json` (SHA-256 `8402cbdcc9c2a20bfad9bd9e1f6fb2596ecbf59a209605dba79fa5c0434b2e5f`). Do not stage this candidate through the pinned installer until the explicit pin proposal and installer rollback behavior have both been resolved, reviewed, and approved. Recompute all hashes independently immediately before transfer and on the VM. Any mismatch stops the procedure.

Use a fresh, empty root-owned mode-0700 staging directory. Copy only the approved runner, gateway support, sudoers draft, canary, installer, and manifest/report bundle. Make copied files root-owned and non-writable by non-root; verify exact file inventory, SHA-256, modes, owners, ACLs, and symlink-free paths. Never reuse or overwrite an unknown staging tree. Check path parents against symlink substitution. Validate sudoers draft with `/usr/sbin/visudo -cf <staged-draft>`; inspect the exact allowlist and ensure there is no shell, wildcard, or canary authorization. Review unit content and systemd properties without installing it.

## 5. Run canary and verify receipt

Only after separate approval for root canary execution, verify staged candidate digests and fixture/mount conditions again. Run the staged canary with `--record` exactly once. It must exit successfully and report confirmed systemd/cgroup termination, including `nosuid` properties for writable mounts. Verify the receipt is canonical JSON, root-owned mode 0600, ACL-free, and binds the approved runner/canary hashes, current boot ID, and current systemd version; check its freshness (24 hours). The canary archives an old receipt by hard link before atomic publication of the replacement; preserve canonical receipt, archives, lock file, and all markers. On timeout, interruption, uncertain termination, or publication error, do not install or retry; retain all evidence and have the administrator investigate.

## 6. Backup and controlled installation

Obtain fresh explicit approval naming the exact three destination files and a unique nonexistent path under `/var/lib/athena-alpha-m1/backups/`. Confirm the backup root is root-owned mode 0700 and ACL-free, capacity is adequate, no prior marker exists, and the D6 evidence remains untouched. **Do not run the current installer candidate:** its EXIT trap calls `rollback_all` automatically after a post-marker error, crossing the separately approved rollback boundary. Installation remains blocked until a reviewed installer change removes that automatic target restoration and preserves the partial state for explicit recovery. Once corrected and approved, the installer must validate staged hashes, build, receipt, and paths, record prior runner/support/sudoers state, and replace files using separate atomic renames. This is atomic per file, not as one transaction. SIGKILL or power loss can still leave mixed generations. Do not start with a pending `install-started` marker or partial state.

After approval, invoke the reviewed installer `install` action with that exact backup path. Capture all output. Then verify all three installed file hashes, owner/mode/ACL and symlink status; run `visudo -c`; check the installed systemd policy text. Preserve the backup unchanged. If command failure occurs after `install-started`, or the host loses power, do not rerun install or automatically rollback. Record and inspect marker states and all destinations; stop for explicit recovery review.

## 7. Campaign preparation and execution

Before campaign approval, repeat VM identity and boot-id checks, compare all approved hashes, check receipt freshness, verify exactly 18 privileged CTest entries against the orchestrator's exact expected set, and verify the protected build artifact manifest. Keep D6 and fixture untouched. Confirm result and system logs have protected, unique destinations and sufficient capacity. Require a separate campaign approval naming the VM, boot ID, runner digest, build manifest digest, and the exact 18-case list.

Only then run the reviewed orchestrator once from the repository root:

```sh
python3 qualification/run-alpha-m1-privileged.py --build-dir /opt/athena-qualification/build
```

Do not invoke privileged CTest cases directly or run them through plain `ctest`. Preserve complete orchestrator output, per-case logs, `results.json`, `results.tsv`, preflight inventory, runner logs, receipts, and sidecars. Reconcile exactly 18 outcomes and report PASS, FAIL, SKIP, TIMEOUT, BLOCKED, NOT_RUN, and infrastructure/evidence errors separately. Every test must have a recorded result; anything other than PASS blocks qualification. Qualification is a distinct review and is not claimed by this procedure.

## 8. Approved rollback and recovery

Rollback requires a new explicit administrator approval, the exact backup path, and a fresh assessment of VM identity, unit/process state, target metadata, and evidence. Never rollback simply because a canary/test failed. First verify the installer backup manifest, exact payload/marker inventory, file type, uid/gid/mode, ACL absence, and SHA-256; inspect all three current destinations and ensure no symlink or unexpected file. If any check fails, preserve everything and stop.

With approval, use the reviewed installer's `rollback` action and the exact backup path. It restores files individually, so interruption can leave mixed generations. Run `visudo -c` and verify all destination hashes, owners, modes, ACLs, and unit state afterwards. Preserve backup, manifest, rollback status markers, logs, receipts, canary archives, and all test evidence; no cleanup is authorized. `PARTIAL ROLLBACK`, I/O failure, SIGKILL, or power loss requires manual recovery planning from the verified manifest; do not blindly rerun or delete markers.

## Current known gates

- VM process-descendant absence has not been established by the container; the supplied failed-unit observations are insufficient.
- The supplied VM fixture/D6 metadata and protected build must be recollected on the VM and reconciled.
- Gateway and canary were reported absent at their target install paths; no installation has been performed here.
- The installer automatically rolls back after some post-marker install failures; Security marks this an authorization blocker. The installer must be corrected and re-reviewed before use.
- Candidate runner/support/canary pin reconciliation and Security review are required before the installer can accept the candidate.
- Receipt must be produced and checked on the target boot/systemd combination.
- Installation, approval, and execution of all 18 privileged tests remain pending.

