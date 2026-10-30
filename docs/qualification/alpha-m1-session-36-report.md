# Monad Alpha M1 — Session 36: Evidence Reconciliation & Administrative Readiness

**Disposition: BLOCKED. Alpha M1 is not qualified and is not ready for installation.** This report reconciles the read-only Session 35 capture and defines separately approvable, read-only follow-up acquisition. It does not authorize or perform administrative operations.

## Scope and evidence identity

Evidence reviewed: `/home/alessandro/monad-alpha-m1-evidence/alpha-m1-vm-evidence-20261021T145020Z-86810/`. The original directory was only read; it was not changed, renamed, copied, or cleaned. The Session 34B candidate and D6 residual are preserved.

`checks.tsv` contains 150 controls: **109 PASS, 34 BLOCKED, 7 NOT RUN**, with no FAIL or TIMEOUT. PASS records describe what this capture observed; they do not by themselves authenticate the VM, snapshot, or execution context. `report.md` explicitly says that the snapshot was not authenticated and that privileged tests, canary, installation, rollback, cleanup, service/cgroup mutation, and mount operations were not run. `vm-virtualization.txt` reports `oracle`; `vm-snapshot.txt` says the snapshot ID was not observed. The guest identity therefore remains unbound to the approved host snapshot.

## Integrated findings

### Candidate and build reconciliation (Core)

The six candidate hashes in `candidate-*-sha256.txt` match the expected Session 34B pins: installer `2376f43c…28dc7`, manifest `00fd4036…11262`, runner `c68bca67…19def`, support `92c928ca…cd514`, canary `9d9d4d84…a94d2`, and sudoers `263c1197…36c18`. This demonstrates byte integrity against those pins at capture time. It does not establish authorization to promote them, installation state, or source-to-binary lineage. The manifest itself says promotion was not performed.

Both build reconciliations show 7/7 allowlisted artifacts matching manifest-pinned hashes. The protected build is `/opt/athena-qualification/build`; its library hash is `e212420c…995a`, generated `CTestTestfile.cmake` hash `99751015…bd5`, and its reported inventory totals 140 tests. The source path `/opt/athena-qualification` is root-owned mode 0550 but is not a Git repository (`build-protected-provenance.txt` records `fatal: not a git repository`). There is no authenticated source revision/tree binding or captured build attestation for those outputs.

The isolated build is `/tmp/monad-s32-verification-final4-20261021`; its library hash is `bd066e22…253f4`, generated CTest file hash `bca33b19…aef0`, and reported total is 147 tests. Its cache source path points to `/home/alessandro/Projects/monad` and records HEAD `abd6bac4b368427b76f6c98522afde928b418939`. That path/revision observation and the output hashes do not prove which exact source tree, generated inputs, toolchain, environment, or invocation produced the binaries. The source checkout was not a clean, authenticated source snapshot. The 140 vs 147 total-test difference must be explained; it is not resolved by the equal 18-name manifest inventory.

For both builds, integrity means observed bytes equal the fixed manifest values. Provenance requires an independently trusted link from an identified source tree and complete build inputs/toolchain/configuration/invocation to those exact outputs. Obtain an authenticated build record or independently reproduce from the reviewed, fixed source snapshot and compare all allowlisted output hashes. If the protected source tree cannot be tied to a known source snapshot, do not qualify or install its binaries based on hash matches alone. Keep any rebuild isolated; do not overwrite the protected build or candidate.

The two `build-*-privileged-inventory.txt` files explicitly state that the 18 names came from the Session 34B manifest and that generated CTest files were not parsed. They do **not** establish the actual generated CTest inventory. Session 35's reported totals (140 and 147) are useful observations, but the capture deliberately left current generated inventory unrun/unverified. A read-only `ctest --show-only=json-v1` query is needed; it executes no tests.

### Collector and environment findings (Verification)

The failed-unit query is malformed in this capture: `systemd-alpha-failed-units.txt` contains `Unknown command verb 'athena-alpha-m1-*.service'.` The collector supplied a unit glob without a `list-units` verb. Consequently, `alpha-failed-unit-count` is a derivative parse failure, not evidence of zero failed units. In contrast, `systemd-alpha-units.txt` and `alpha-unit-names.txt` show 15 matching Alpha units, each reported `loaded failed failed`. The correction is a read-only query such as `systemctl list-units --failed --type=service ...` or an unfiltered failed-unit list followed by client-side filtering. Do not reset or restart units.

`vm-systemd-version.txt` reports that the executable named `systemd` was unavailable, while multiple `systemctl` and `journalctl` observations succeeded. This is a collector probe/tool-availability issue and does not demonstrate that systemd is absent. `vm-systemd-health.txt` contains `degraded` (the command exited 1); that is an observed environment condition requiring review on the authenticated target.

All 15 captured unit `show` files have an empty `ControlGroup=`. Their paired `.cgroup-events.txt` files correctly remain BLOCKED. `MainPID=0` and failed/inactive-looking unit state do not prove that no descendants remain. `proc-cgroup-v2-root.txt` records that `/sys/fs/cgroup/cgroup.events` does not exist in this capture. `residual-process-gate` is correctly BLOCKED for the same missing unit cgroup. These are insufficient cgroup/process observations, not grounds to infer absence. Verify cgroup mount/layout and correlate unit properties, process list and `/proc/PID/cgroup` on the authenticated VM. If a unit still lacks a usable ControlGroup, the descendant gate stays unresolved until an independently reliable method is reviewed.

Protected-path `EACCES`/`Permission denied` results are access restrictions. They do not prove that fixture, receipt, backup, log, or D6 objects are absent or malformed. No such result is promoted to PASS. The collector's symlink-safe traversal and deliberate refusal to inspect inaccessible targets are conservative; raw-path reads must remain blocked until their path components are authorized and checked.

### Independent Security assessment

Security review disposition: **BLOCKED for staging and activation**. The capture shows `/var/lib/athena-alpha-m1` and `/var/log/athena-alpha-m1` parent directories as root-owned mode 0700 with no observed symlink in the inspected components; this is parent hardening evidence only. It does not establish leaf type, ownership, mode, ACL, link count, content, or hashes for the fixture, canary receipt, backups, or logs. The D6 parent is reported root-owned mode 0700 on ext4, but the residual `athena-d6-8yRDjc` could not be traversed; its identity, contents, mount boundary, and preservation state remain unverified. Keep it untouched.

Protected build tree metadata is root-owned mode 0550 with no observed symlink; matching hashes still do not authenticate provenance. The 15 failed units have no captured ControlGroup, so descendant absence is not demonstrated. Evidence files themselves are in a mode-0700 directory with mode-0600 files, but same-host root can alter them; retain the original tree and record any separately acquired evidence and its digest externally.

Metadata/inventory acquisition and content/hash acquisition should be approved as separate, narrow read-only scopes. Specify the VM/snapshot, exact paths and fields, output destination, retention, and no-mutation boundary. Root reads do not authorize installation, canary, privilege expansion, service/cgroup/mount changes, tests, rollback, or cleanup. Do not use a general root shell as an implied authorization; use an already approved administrative console and fixed commands. Never run workspace binaries as root.

## Matrix of all 34 BLOCKED controls

Evidence paths below are relative to the evidence directory stated above. Rows grouped as “15 controls” list every individual control; each remains independently BLOCKED.

| # | Control | Cause and evidence | Read-only resolution / classification |
|---:|---|---|---|
| 1 | `vm-systemd-version` | `vm-systemd-version.txt`: `systemd` executable not found; other systemd tools did run. | Query `systemctl --version` or approved `/usr/bin/systemd --version`; collector probe/tool availability, not proof systemd is absent. |
| 2 | `vm-systemd-health` | `vm-systemd-health.txt`: `degraded` (exit 1). | On the identity-verified VM, inspect failed units and manager state; actual captured environment condition, cause not yet diagnosed. |
| 3 | `vm-snapshot` | `vm-snapshot.txt`: snapshot not queried. | Host-side VirtualBox identity and snapshot observation; deliberately omitted, requires host operator. |
| 4 | `systemd-alpha-failed-units` | `systemd-alpha-failed-units.txt`: `Unknown command verb 'athena-alpha-m1-*.service'.` | Correct the query to `systemctl list-units --failed ...` or filter an unfiltered failed list; collector/query defect. |
| 5 | `alpha-failed-unit-count` | `alpha-failed-unit-names.txt` empty because row 4 produced an error; `systemd-alpha-units.txt` lists 15. | Recompute from corrected read-only result. Derivative collector parse failure, not zero failed units. |
| 6 | `unit-athena-alpha-m1-28cc5aa5ec9a7fd1e7c6b715.service.cgroup-events` | Paired `.show.txt` has empty `ControlGroup=`; `.cgroup-events.txt` refuses inference. | Authorized unit/cgroup inspection; keep blocked unless populated state is directly established. |
| 7 | `unit-athena-alpha-m1-2ba07ed2f4ad03055baff910.service.cgroup-events` | Same empty `ControlGroup=` in paired show file. | Same per-unit cgroup and process reconciliation. |
| 8 | `unit-athena-alpha-m1-40ca148184e8131b74789e21.service.cgroup-events` | Same empty `ControlGroup=` in paired show file. | Same per-unit cgroup and process reconciliation. |
| 9 | `unit-athena-alpha-m1-43ff39862d99ed8e68d729ae.service.cgroup-events` | Same empty `ControlGroup=` in paired show file. | Same per-unit cgroup and process reconciliation. |
| 10 | `unit-athena-alpha-m1-45f1c7159a426941d20fed8a.service.cgroup-events` | Same empty `ControlGroup=` in paired show file. | Same per-unit cgroup and process reconciliation. |
| 11 | `unit-athena-alpha-m1-5c40304f1dae004fc15e3830.service.cgroup-events` | Same empty `ControlGroup=` in paired show file. | Same per-unit cgroup and process reconciliation. |
| 12 | `unit-athena-alpha-m1-6633d84535fa91142caf6bd5.service.cgroup-events` | Same empty `ControlGroup=` in paired show file. | Same per-unit cgroup and process reconciliation. |
| 13 | `unit-athena-alpha-m1-666aa836f5d15842d47bdef3.service.cgroup-events` | Same empty `ControlGroup=` in paired show file. | Same per-unit cgroup and process reconciliation. |
| 14 | `unit-athena-alpha-m1-716ab14bcbb4b154f5953042.service.cgroup-events` | Same empty `ControlGroup=` in paired show file. | Same per-unit cgroup and process reconciliation. |
| 15 | `unit-athena-alpha-m1-7fe9a30496fd95c631ea7c4b.service.cgroup-events` | Same empty `ControlGroup=` in paired show file. | Same per-unit cgroup and process reconciliation. |
| 16 | `unit-athena-alpha-m1-88fb1f423d5274d96d5ce40b.service.cgroup-events` | Same empty `ControlGroup=` in paired show file. | Same per-unit cgroup and process reconciliation. |
| 17 | `unit-athena-alpha-m1-958addae253edbe229d4917b.service.cgroup-events` | Same empty `ControlGroup=` in paired show file. | Same per-unit cgroup and process reconciliation. |
| 18 | `unit-athena-alpha-m1-c720b53df3240534303448a8.service.cgroup-events` | Same empty `ControlGroup=` in paired show file. | Same per-unit cgroup and process reconciliation. |
| 19 | `unit-athena-alpha-m1-fc331e22979bcd4126863547.service.cgroup-events` | Same empty `ControlGroup=` in paired show file. | Same per-unit cgroup and process reconciliation. |
| 20 | `unit-athena-alpha-m1-ff5c72902fe3a6513080384a.service.cgroup-events` | Same empty `ControlGroup=` in paired show file; residual gate references it. | Same per-unit cgroup and process reconciliation; no descendant absence inference. |
| 21 | `proc-cgroup-v2-root` | `proc-cgroup-v2-root.txt`: `/sys/fs/cgroup/cgroup.events` does not exist. | Approved read-only mount/layout inspection and cgroup event read where available; capture environment/layout restriction. |
| 22 | `residual-process-gate` | `system-process-cgroups.txt` lacks usable unit ControlGroup for `...ff5c72902fe3a6513080384a.service`. | Correlate complete unit/process/cgroup views; keep blocked if no reliable descendant proof. |
| 23 | `path-state-root-children` | `path-state-root-children.txt`: `find` permission denied at `/var/lib/athena-alpha-m1`. | Separately approved metadata/inventory read; access restriction, not evidence of absence. |
| 24 | `path-fixture-components` | `path-fixture-components.txt`: EACCES at `ext4-fixture`, target not queried. | Component-by-component no-follow metadata and mount check; then only separately approved read. |
| 25 | `path-receipt-components` | `path-receipt-components.txt`: EACCES at `systemd-canary.json`. | Component-by-component no-follow metadata; separately approve receipt metadata/hash/content scope. |
| 26 | `path-backups-components` | `path-backups-components.txt`: EACCES at `backups`. | Component-by-component no-follow metadata; inspect manifest only after approval. |
| 27 | `path-log-root-children` | `path-log-root-children.txt`: `find` permission denied. | Separately approve bounded log metadata inventory; logs may contain sensitive data. |
| 28 | `path-d6-parent-children` | `path-d6-parent-children.txt`: permission denied listing `/var/tmp/athena-d67-ext4`. | Separately approved no-follow metadata inventory; preserve all D6 state. |
| 29 | `path-d6-residual-components` | `path-d6-residual-components.txt`: EACCES at `athena-d6-8yRDjc`; target not queried. | Approved metadata/mount inspection only; do not read or mutate residual contents absent separate scope. |
| 30 | `backups-evidence-file-sha256` | `backups-evidence-file-sha256.txt`: backups root not safely readable. | After path/type checks, separately approve hashes of exact receipt/backup/transaction evidence files. |
| 31 | `logs-evidence-file-acls` | `logs-evidence-file-acls.txt`: `find` permission denied. | Separately approved ACL/metadata inventory of exact evidence files; do not broaden scope to all log content. |
| 32 | `logs-evidence-file-sha256` | `logs-evidence-file-sha256.txt`: `find` permission denied. | After no-follow path validation, separately approve hashes of identified regular evidence files. |
| 33 | `build-protected-provenance` | `build-protected-provenance.txt`: source path is not a Git repository; hashes/cache do not bind source to outputs. | Authenticate source snapshot and build record or perform an isolated reproducible rebuild and compare hashes. Evidence insufficiency, not a collector defect. |
| 34 | `build-isolated-provenance` | `build-isolated-provenance.txt`: source path and HEAD observed, but no authenticated input/build/output link. | Establish exact clean source tree and full build inputs/invocation; provide trusted attestation or isolated reproducible rebuild. Evidence insufficiency. |

## Approved-readiness acquisition plan (commands for review only)

**Nothing in this section was run.** Commands are proposals for separate, explicit approval and must be executed only in the identified context. Save output to a new, private evidence directory outside the checkout and protected state. Preserve the original Session 35 directory unchanged. Do not combine these approvals with a mutation approval.

### 1. Bind the VM to the approved VirtualBox snapshot (host only)

On the authorized VirtualBox host, an operator first identifies the exact VM UUID; then run read-only queries with that literal UUID:

```sh
VBoxManage list vms
VBoxManage showvminfo "<approved-VM-UUID>" --machinereadable
VBoxManage snapshot "<approved-VM-UUID>" list --details
```

Record VM UUID/name, current snapshot UUID/name, snapshot list, host UTC timestamp, and bind them to guest machine-id and boot-id from this capture/new authenticated capture. These commands inspect metadata; do not take, restore, delete, or switch snapshots.

### 2. Reconcile systemd units and residual processes (authenticated VM)

First capture manager state and query failed units without the malformed positional glob:

```sh
date -u +%Y-%m-%dT%H:%M:%SZ
id
systemctl --version
systemctl is-system-running
systemctl --failed --type=service --no-legend --no-pager --plain --full
systemctl list-units --all --type=service --no-legend --no-pager --plain --full 'athena-alpha-m1-*.service'
systemctl list-unit-files --no-legend --no-pager 'athena-alpha-m1-*.service'
ps -eo pid,ppid,uid,stat,comm,args --sort=pid
```

For each exact unit name returned, inspect properties and journal read-only:

```sh
systemctl show '<exact-unit-name>' --property=Id,LoadState,ActiveState,SubState,MainPID,ControlGroup,Result,ExecMainCode,ExecMainStatus
journalctl --no-pager -n 100 -o short-iso-precise -u '<exact-unit-name>'
```

For every process that could be associated with a unit, capture `/proc/<PID>/cgroup`; inspect `cgroup.events` only under a validated cgroup mount/path and record `populated` where available. The current 15 blank ControlGroup values are unresolved: do not substitute a guessed cgroup path. Do not reset, start, stop, restart, kill, or otherwise mutate units/processes.

### 3. Read protected path metadata and inventory (existing approved admin console)

Approve this metadata-only pass separately. It checks fixed paths and does not read file contents:

```sh
namei -l -- /var/lib/athena-alpha-m1/ext4-fixture /var/lib/athena-alpha-m1/systemd-canary.json /var/lib/athena-alpha-m1/backups /var/log/athena-alpha-m1 /var/tmp/athena-d67-ext4/athena-d6-8yRDjc
stat -c 'type=%F mode=%a uid=%u gid=%g links=%h path=%n' -- /var/lib/athena-alpha-m1 /var/lib/athena-alpha-m1/ext4-fixture /var/lib/athena-alpha-m1/systemd-canary.json /var/lib/athena-alpha-m1/backups /var/log/athena-alpha-m1 /var/tmp/athena-d67-ext4 /var/tmp/athena-d67-ext4/athena-d6-8yRDjc
getfacl -cp -- /var/lib/athena-alpha-m1 /var/lib/athena-alpha-m1/ext4-fixture /var/lib/athena-alpha-m1/systemd-canary.json /var/lib/athena-alpha-m1/backups /var/log/athena-alpha-m1 /var/tmp/athena-d67-ext4 /var/tmp/athena-d67-ext4/athena-d6-8yRDjc
findmnt -T /var/lib/athena-alpha-m1/ext4-fixture -o TARGET,SOURCE,FSTYPE,OPTIONS
findmnt -T /var/tmp/athena-d67-ext4/athena-d6-8yRDjc -o TARGET,SOURCE,FSTYPE,OPTIONS
find -P /var/lib/athena-alpha-m1/backups -mindepth 1 -maxdepth 4 -printf '%y mode=%m uid:gid=%U:%G links=%n %p\n'
find -P /var/tmp/athena-d67-ext4/athena-d6-8yRDjc -mindepth 1 -maxdepth 4 -printf '%y mode=%m uid:gid=%U:%G links=%n %p\n'
```

If any component is a symlink, unexpected owner/mode/ACL, mount, or type, stop and preserve BLOCKED. Do not follow symlinks. Do not traverse D6 object contents. Only after path and regular-file checks, seek a distinct approval naming exact receipt/backup/transaction/log evidence files for no-follow hash acquisition; seek another explicit scope before reading receipt or manifest contents. Keep sensitive logs out of broad reports.

### 4. Verify generated CTest inventory without executing tests

On the identity-verified VM, after build path metadata is approved and safe, obtain inventory only:

```sh
ctest --test-dir /opt/athena-qualification/build --show-only=json-v1 -L privileged
ctest --test-dir /opt/athena-qualification/build --show-only=json-v1
ctest --test-dir /tmp/monad-s32-verification-final4-20261021 --show-only=json-v1 -L privileged
ctest --test-dir /tmp/monad-s32-verification-final4-20261021 --show-only=json-v1
```

These `--show-only` commands enumerate and do not run cases. Compare exact generated names/counts with the Session 34B 18-name allowlist and reconcile the 140 vs 147 totals. Do not invoke `ctest` without `--show-only`; do not run test executables.

### 5. Establish build provenance

First seek an authenticated build record that identifies the exact source tree/archive digest, all source/generated inputs, compiler and linker versions, CMake options, dependency versions, build command, environment, and hashes of every output artifact. The record must be independently protected or signed and bind to the currently observed hashes. For the protected source tree with no Git metadata, identify its trusted archive/snapshot origin and digest; do not infer a revision.

If such records do not exist, propose a separately reviewed, unprivileged isolated reproducible build from an approved immutable source snapshot and compare the allowlisted hashes. A rebuild can establish reproducibility for that new build; it does not retroactively prove lineage of the existing protected build. Do not overwrite either existing build, update manifest pins silently, or install artifacts as part of provenance work.

## Gates still required before Alpha M1 installation

1. Authenticate VM UUID and approved snapshot on the VirtualBox host; correlate guest machine-id/boot-id.
2. Resolve systemd health and collector query defects; enumerate all Alpha units, failed units, process descendants, and usable cgroup state with no unresolved residual-process gate.
3. Obtain approved read-only metadata and evidence inventory for fixture, canary receipt, backups, log evidence, and D6 residual; verify no-follow path safety, ownership, modes, ACLs, mount, expected content/hashes. Preserve D6 and all historical evidence.
4. Verify live generated CTest inventories for both builds against the exact 18-case allowlist; explain 140 vs 147 total tests without executing tests.
5. Establish source-to-binary provenance for the protected build and any proposed isolated build; matching pins alone are insufficient.
6. Complete an independent final Security review of the exact artifacts, VM evidence and administrative plan; obtain explicit human approval for any later staging, canary, installation, sudoers/gateway change, privileged campaign, rollback or cleanup. Read-only approval does not grant any of these.
7. Only after the above gates, follow the current admin staging runbook and complete its separately approved canary/install/test/evidence review sequence. Until every required privileged case and evidence review passes, retain **INCOMPLETE** status.

## Independent review record

- **Core: PASS for evidence reconciliation; qualification gate remains BLOCKED.** Candidate and build hashes reconcile to the fixed manifest, while neither build's source-to-output provenance is proven. Additional proof is specified above.
- **Verification: PASS for review; no test execution.** All 150 controls were classified. The failed-unit command and its dependent count are collector defects; missing cgroups and protected-path access denials remain unresolved observations/restrictions; CTest inventory PASS rows are manifest-derived only.
- **Security: BLOCKED for staging/activation.** Parent directory protections are observed, but protected leaf identities/evidence and D6 residual are not verified. No mutation is authorized by this report.
- **Operations: plan prepared; not executed.** Host VirtualBox, authenticated-VM read-only, protected metadata, CTest show-only, and provenance procedures are listed above for separate review/approval.

No privileged command, collector, canary, CTest case, installation, rollback, cleanup, service/cgroup/mount mutation, commit, or push was performed for Session 36.
