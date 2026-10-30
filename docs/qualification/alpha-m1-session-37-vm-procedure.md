# Alpha M1 Session 37 — VM read-only evidence procedure

**Status: draft for explicit review and approval. This document is not authorization to execute.** It defines a bounded, read-only acquisition on the designated Debian qualification VM and its VirtualBox host. It does not authorize staging, canary execution, installation, test execution, rollback, cleanup, or any system, service, cgroup, filesystem, fixture, or mount change. Do not run these commands in the development container. No command in this procedure was executed while preparing it.

Session 35 evidence remains historical evidence. Preserve `/home/alessandro/monad-alpha-m1-evidence/alpha-m1-vm-evidence-20261021T145020Z-86810` byte-for-byte and retain the Session 34B candidate and the D6 residual `athena-d6-8yRDjc` in place. Put any new capture in a fresh, private evidence directory; never reuse, overwrite, rename, or clean an existing evidence directory.

## Approval and execution boundary

Before execution, obtain a recorded human decision that identifies the exact VM UUID, approved snapshot/disposable-state identity, operator, date/time window, commands, source candidate directory, existing private evidence parent, and evidence retention/transfer method. Obtain distinct approval for any read requiring administrative access, specifying exact paths and fields. A read-only approval does not authorize a system change or execution of a privileged test. Do not invoke `sudo`; where a protected read is approved, use the separately authorized administrative console and only the listed read commands. Execute protected-path checks sequentially: run `namei` first, review its result, then obtain approval for the next exact command. Do not paste or execute a whole command block. If a component is a symlink, has unexpected ownership/mode/ACL, or otherwise differs from the reviewed path, stop before any command that could follow it; request a reviewed no-follow acquisition method. Do not run workspace or build binaries as root.

Run the collector as an ordinary, non-root VM user. It creates a new timestamp/PID child directory under the approved existing evidence parent and refuses collisions rather than overwriting. It does not itself establish the VirtualBox snapshot identity. Inspect its `checks.tsv`, `report.md`, raw files, and the CTest inventory reports; successful exit is not an overall PASS. Preserve all `BLOCKED`, `FAIL`, `TIMEOUT`, and `NOT RUN` classifications as recorded until the corresponding evidence has been reviewed. Do not infer PASS from missing or inaccessible evidence.

The examples below are command proposals for review. Replace placeholders only with paths/identifiers recorded in the approval. Commands that write a capture file write only to the newly approved evidence directory; no command writes to the VM's administrative paths.

## 1. Identify the approved VM and snapshot (VirtualBox host)

Run on the authorized VirtualBox host. First identify the exact VM UUID, then query that literal UUID:

```sh
VBoxManage list vms
VBoxManage showvminfo "<approved-VM-UUID>" --machinereadable
VBoxManage snapshot "<approved-VM-UUID>" list --details
```

Record host UTC time, VM UUID/name, current snapshot UUID/name, and the snapshot inventory. Bind these observations to the guest machine ID and boot ID collected in section 2. A guest `systemd-detect-virt` result such as `oracle` identifies a virtualization type; it does not identify the approved VM or snapshot. Do not take, restore, switch, or delete a snapshot.

## 2. Reconcile systemd units, processes, and cgroups (authenticated VM)

Collect the manager identity and inventory as structured JSON. Session 35's failed-unit command passed a glob as a positional verb and returned `Unknown command verb`; the integrated collector now uses the two commands below and filters/parses the JSON strictly. JSON fields are independent of the localized human-readable unit table. Do not substitute a localized table or infer an empty inventory from a command/parser failure.

```sh
date -u +%Y-%m-%dT%H:%M:%SZ
id
cat /etc/os-release
uname -a
cat /etc/machine-id
cat /proc/sys/kernel/random/boot_id
systemd-detect-virt
systemctl --version
systemctl is-system-running
systemctl list-units --all --type=service --output=json --no-pager
systemctl list-unit-files --type=service --output=json --no-pager
ps -eo pid,ppid,uid,stat,comm,args --sort=pid
```

Preserve both raw JSON outputs. Verify that each root value is an array; each unit row has unique `unit`, `load`, `active`, and `sub` string fields; and each unit-file row has unique `unit_file` and `state` string fields. Apply the reviewed strict parser and the Alpha M1 name allowlist. The Session 37 collector produces `alpha-unit-states.tsv`, including `unit`, `classification`, `load_state`, `active_state`, `sub_state`, and `unit_file_state`, and counts by classification. Review these states separately:

- **failed:** a loaded unit with `ActiveState=failed`;
- **active:** a loaded unit with `ActiveState=active`;
- **inactive:** a loaded unit with `ActiveState=inactive`;
- **not-loaded:** unit-file inventory exists without a matching loaded unit row, or the runtime `LoadState` is not `loaded`;
- **transitional/other:** loaded units in `activating`, `deactivating`, `reloading`, or `maintenance` state.

Do not merge these classifications. Failed is not the same as not-loaded or inactive; transitional states are unresolved until reviewed. A nonzero command result, malformed or incomplete JSON, duplicate/unexpected unit name, missing field, or unknown enum is a **tool/parser error**: retain the raw output and classify inventory/counts as BLOCKED. Do not parse an error as zero units or zero failed units. The previous Session 35 glob-as-verb error is a collector/query defect; the new JSON queries avoid that command shape but their result still requires review.

For every exact Alpha M1 unit name in the validated structured inventory, query its detailed properties and journal individually:

```sh
systemctl show '<exact-unit-name>' --property=Id,LoadState,ActiveState,SubState,MainPID,ControlGroup,Result,ExecMainCode,ExecMainStatus
journalctl --no-pager -n 100 -o short-iso-precise -u '<exact-unit-name>'
```

Correlate each unit with PID, PPID, command, and `/proc/<PID>/cgroup` observations. First record the cgroup filesystem/mount layout with `findmnt -T /sys/fs/cgroup -o TARGET,SOURCE,FSTYPE,OPTIONS`. Read `cgroup.events` only for a nonempty `ControlGroup` returned by `systemctl show`, after checking that it is an absolute cgroup-relative path without `..` and resolves under the verified cgroup mount. Record the exact resolved path and `populated` value. Do not guess a path when `ControlGroup` is empty, use `MainPID=0` as descendant proof, or interpret a missing cgroup file as an empty cgroup. Any unexplained process, absent control group, or inaccessible events file leaves the residual-process gate BLOCKED for operator review. Do not reset, start, stop, restart, signal, or kill any unit or process.

## 3. Inspect fixture and D6 metadata (separately approved administrative read)

The following fixed paths are the only targets for the initial metadata pass. Run only in an already authorized administrative console after approval explicitly names the paths. Execute the commands below sequentially, with review and approval between steps; do not paste the complete block. First run only this component inspection:

```sh
namei -l -- /var/lib/athena-alpha-m1/ext4-fixture /var/tmp/athena-d67-ext4 /var/tmp/athena-d67-ext4/athena-d6-8yRDjc
```

Review every component in the output. If any component is a symlink, has unexpected ownership/mode/ACL, or does not match the approved path, stop here. Do not run subsequent path-based commands; request an independently reviewed no-follow acquisition method. Only when the path review passes, obtain approval for the next exact command, run it individually, and review its result. Stop on any unexpected metadata before requesting approval for another command. Repeat that approval/run/review sequence for each later command:

```sh
stat -c 'type=%F mode=%a uid=%u gid=%g links=%h path=%n' -- /var/lib/athena-alpha-m1/ext4-fixture /var/tmp/athena-d67-ext4 /var/tmp/athena-d67-ext4/athena-d6-8yRDjc
```

```sh
getfacl -cp -- /var/lib/athena-alpha-m1/ext4-fixture /var/tmp/athena-d67-ext4 /var/tmp/athena-d67-ext4/athena-d6-8yRDjc
```

```sh
findmnt -T /var/lib/athena-alpha-m1/ext4-fixture -o TARGET,SOURCE,FSTYPE,OPTIONS
```

```sh
findmnt -T /var/tmp/athena-d67-ext4/athena-d6-8yRDjc -o TARGET,SOURCE,FSTYPE,OPTIONS
```

After the exact target paths and mount points have passed review, obtain approval for each names-only inventory command separately. Run one command, review its output, and stop on unexpected metadata before seeking approval for the next:

```sh
find -P /var/lib/athena-alpha-m1/ext4-fixture -xdev -mindepth 0 -maxdepth 3 -printf '%y mode=%m uid:gid=%U:%G links=%n %p\n'
```

```sh
find -P /var/tmp/athena-d67-ext4/athena-d6-8yRDjc -xdev -mindepth 0 -maxdepth 4 -printf '%y mode=%m uid:gid=%U:%G links=%n %p\n'
```

The D6 metadata inventory must retain the residual name and its known evidence paths, including `generations`, `projection`, `store`, and `generations/.athena-locks/state.lock` when present. `-xdev` prevents the names-only listing from crossing onto a nested filesystem. Do not read file payloads, copy, clean, delete, rename, chmod, chown, or mount over the fixture or D6 residual. If any path is a symlink, has an unexpected owner/mode/ACL, crosses an unexpected mount, or cannot be inspected, stop and preserve the result as BLOCKED. Do not infer that the residual is absent.

## 4. Inspect receipt, backups, transaction records, and logs (separately approved)

The metadata and names-only inventory are a separate approval scope from reading content or hashing evidence. Execute these checks sequentially, not as a pasted block. First inspect only the path components:

```sh
namei -l -- /var/lib/athena-alpha-m1 /var/lib/athena-alpha-m1/systemd-canary.json /var/lib/athena-alpha-m1/backups /var/log/athena-alpha-m1
```

Review every component. If any component is a symlink, has unexpected ownership/mode/ACL, or differs from the exact approved path, stop and do not run later path-based commands. Request a reviewed no-follow acquisition method. Only after a clean path review, obtain approval for the next exact metadata command, run it individually, and review its result. Stop on unexpected metadata before requesting approval for another command. Repeat that approval/run/review sequence for each command:

```sh
stat -c 'type=%F mode=%a uid=%u gid=%g links=%h path=%n' -- /var/lib/athena-alpha-m1 /var/lib/athena-alpha-m1/systemd-canary.json /var/lib/athena-alpha-m1/backups /var/log/athena-alpha-m1
```

```sh
getfacl -cp -- /var/lib/athena-alpha-m1 /var/lib/athena-alpha-m1/systemd-canary.json /var/lib/athena-alpha-m1/backups /var/log/athena-alpha-m1
```

```sh
find -P /var/lib/athena-alpha-m1/backups -xdev -mindepth 0 -maxdepth 4 -printf '%y mode=%m uid:gid=%U:%G links=%n %p\n'
```

```sh
find -P /var/log/athena-alpha-m1 -xdev -mindepth 0 -maxdepth 4 -printf '%y mode=%m uid:gid=%U:%G links=%n %p\n'
```

Only after a second, explicit read/hash scope identifies exact regular files and their verified non-symlink paths, acquire metadata and SHA-256 for those paths. The approved list may include the canonical receipt, receipt archives, transaction/backup manifests, markers, and named run logs; it must not expand to all log contents by default. Use exact file arguments, not broad wildcards:

```sh
stat -c 'type=%F mode=%a uid=%u gid=%g links=%h path=%n' -- '<approved-exact-file-1>' '<approved-exact-file-2>'
getfacl -cp -- '<approved-exact-file-1>' '<approved-exact-file-2>'
sha256sum -- '<approved-exact-file-1>' '<approved-exact-file-2>'
```

Do not `cat`, parse, archive, or copy receipts, backup payloads, or logs without a separate content-acquisition approval. Preserve every receipt, backup, transaction marker, log, and report in place; no cleanup or recovery action is authorized.

## 5. Verify generated CTest inventory without running tests

The collector does not run CTest. Run it as the approved ordinary VM user, in the reviewed checkout, using a pre-existing canonical private evidence parent outside the checkout and all protected paths:

```sh
cd /absolute/reviewed/checkout
test "$(pwd -P)" = /absolute/reviewed/checkout
test -d /absolute/private/evidence-parent
bash qualification/alpha-m1-vm-evidence-collector.sh \
  --output-parent /absolute/private/evidence-parent \
  --candidate-dir /absolute/reviewed/checkout/qualification
```

The `--candidate-dir` argument may be omitted only if that is the reviewed invocation. The collector must not be run as root or with `sudo`. It creates a new evidence child directory; record the printed path and review `checks.tsv`, `report.md`, all raw evidence, and these integrated generated inventory reports:

```text
build-protected-ctest-inventory.json
build-isolated-ctest-inventory.json
build-protected-ctest-helper-sha256.txt
build-isolated-ctest-helper-sha256.txt
```

If a separate checker invocation is needed, first review and record the exact checker SHA-256 and approve its fixed inputs. Run it as a normal user for each fixed build directory:

```sh
python3 -I qualification/alpha-m1-ctest-inventory.py \
  --build-dir /opt/athena-qualification/build \
  --manifest /absolute/reviewed/checkout/qualification/alpha-m1-build-reconciliation-session34b-candidate.json
python3 -I qualification/alpha-m1-ctest-inventory.py \
  --build-dir /tmp/monad-s32-verification-final4-20261021 \
  --manifest /absolute/reviewed/checkout/qualification/alpha-m1-build-reconciliation-session34b-candidate.json
```

Capture checker output only through the approved evidence capture method. Confirm that the checker version and manifest hash are recorded. Compare the actual generated inventory against the exact Session 34B 18-name allowlist and account for the reported 140 versus 147 total-test counts. Inventory enumeration is not test execution. Do not use `ctest -N`, plain `ctest`, a test label run, or any test executable; do not run the checker as root. A checker PASS does not qualify the binaries or prove their provenance.

## 6. Establish build provenance

First inspect any existing authenticated build attestation that directly binds the exact source snapshot/tree digest, generated inputs, compiler/linker and dependency versions, CMake configuration, build command/environment, and output artifact hashes. The attestation must be independently protected or signed and match the observed artifact hashes. A cache source path, Git HEAD string, manifest match, or matching binary hash alone is integrity evidence and does not establish source-to-binary provenance.

Session 35 found no Git metadata at `/opt/athena-qualification` and no authenticated source/build/output link for the isolated build either. If authenticated original records do not exist, classify provenance as BLOCKED and submit a separately reviewed proposal for a reproducible build in a new isolated directory from an approved immutable source snapshot. That new build may establish reproducibility for itself; it does not retroactively establish the lineage of the existing protected build. Do not overwrite either existing build, alter manifest pins, or install a rebuilt artifact.

## 7. Reconcile Session 34B candidate hashes and pins

Record the reviewed Session 34B manifest digest and the six candidate digests from the approved VM-side capture. Compare each exact file with the fixed Session 34B values below; this is byte-integrity reconciliation only. It does not prove transfer authorization, installation state, or build provenance.

| Candidate | Expected SHA-256 |
|---|---|
| `install-athena-alpha-m1-admin.sh` | `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7` |
| `alpha-m1-build-reconciliation-session34b-candidate.json` | `00fd4036bf0f3873f5c2a0e932c912b4709460fa52c35a1c2564016a92811262` |
| `athena-alpha-m1-runner.py` | `c68bca67a7a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` |
| `gateway_support.py` | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` |
| `athena-alpha-m1-systemd-canary.py` | `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` |
| `athena-alpha-m1-sudoers` | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` |

Require an independently reviewed manifest and fixed paths. Any mismatch is a stop; do not regenerate or promote pins in this read-only procedure. Distinguish candidate checkout hashes, transferred-file hashes, and installed-file hashes. Any further transfer or installed-file read is a separately approved scope.

## Result handling and remaining gates

Keep a contemporaneous record of the approving authority, exact commands, operator, host/guest UTC timestamps, VM UUID, snapshot UUID, guest machine ID and boot ID, output paths, checker and collector hashes, and evidence directory digest. Preserve the original Session 35 directory unchanged. Store new evidence only under the separately approved private evidence parent, retain raw output, and review all classifications independently. Same-host root can alter local evidence and sidecar hashes; record an external digest or approved independent evidence reference if required by the review.

Any inaccessible path, symlink, ownership/mode/ACL anomaly, unexplained unit/process, missing cgroup proof, hash mismatch, inventory mismatch, stale or invalid receipt, or disagreement between host and guest identity is a stop condition. Keep that item BLOCKED or FAIL as observed and request operator review. Do not “fix” observations by changing system state.

Before any installation decision, the independent reviewers must accept VM/snapshot identity, systemd/process/cgroup reconciliation, fixture and D6 preservation evidence, receipt/backup/log evidence, exact generated CTest inventory, source-to-binary provenance for the approved build, and Session 34B candidate hashes. Separate human approvals remain necessary for staging, live canary/receipt creation, installation or sudoers/gateway changes, privileged campaign, rollback, and cleanup. None is granted by this document or by completion of read-only collection. Alpha M1 remains unqualified until the approved privileged campaign and evidence review pass every required gate.
