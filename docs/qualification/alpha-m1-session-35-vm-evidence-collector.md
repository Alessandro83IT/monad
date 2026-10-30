# Session 35 VM evidence collector

## Purpose and limits

`qualification/alpha-m1-vm-evidence-collector.sh` is for manual execution in a shell on the dedicated Debian qualification VM. Do not run it in the development container and present that output as VM evidence. The script does not infer snapshot identity; record the snapshot/disposable-state ID independently through the authorized hypervisor or VM interface.

It writes only to a new, private mode-0700 directory below a caller-selected existing output parent. It refuses a noncanonical or symlinked output/candidate directory, a parent not owned by the caller, a group/world-writable output parent, and an output-name collision. It does not overwrite an existing directory. On interruption or output errors it retains partial evidence and does not clean it up. Exit status 0 means only that collection reached its end; review every row in `checks.tsv` and every captured file.

Some evidence can contain system log messages, process command lines, paths, and receipt/backup hashes. Protect the output directory as sensitive evidence and use the approved evidence transfer location.

The collector never invokes `sudo`, executes a target binary or CTest, starts/stops/resets a service, changes a cgroup, modifies mounts, installs files, rolls back, or cleans up. It refuses execution as UID 0. The exact 18-name privileged inventory is read from the Session 34B manifest whose fixed hash is checked; generated CTest files are hashed but never parsed or executed. Current build CTest inventory is therefore `NOT RUN` and must be reconciled separately without running any privileged cases. Build paths and files are checked for symlink components before inspection; symlink or permission anomalies are recorded as `BLOCKED` and not traversed. A hash or manifest-inventory mismatch is `FAIL`. Deliberately omitted privileged actions are `NOT RUN`.

## Prepare the manual execution

Transfer the reviewed collector and the six baseline candidate files into the approved VM review checkout or candidate directory using the separately authorized transfer process. Do not place the evidence output inside the checkout, protected state directories, fixture, backup tree, or D6 paths. The directory containing the six candidate files must contain:

```text
install-athena-alpha-m1-admin.sh
alpha-m1-build-reconciliation-session34b-candidate.json
athena-alpha-m1-runner.py
gateway_support.py
athena-alpha-m1-systemd-canary.py
athena-alpha-m1-sudoers
```

Use an already existing, canonical, user-owned output parent that is not group/world writable. Example, after replacing the two paths with the approved VM paths:

```bash
cd /path/to/reviewed/checkout
test -d /path/to/private/evidence-parent
test "$(pwd -P)" = /path/to/reviewed/checkout
bash qualification/alpha-m1-vm-evidence-collector.sh \
  --output-parent /path/to/private/evidence-parent \
  --candidate-dir /path/to/reviewed/checkout/qualification
```

The candidate directory must be canonical and non-symlinked. The script prints the newly created evidence directory. Do not redirect its output to a protected path; copy or archive the finished directory only through the approved evidence handling process.

## Normal-user and administrator-readable checks

Run the collector as an ordinary VM user. It can usually acquire hostname, UID, Debian/kernel/architecture, machine and boot IDs, virtualization detection, systemd version, many `systemctl show/list` properties, the current process list, readable `/proc` cgroups, manifest inventory, candidate/build hashes, and metadata for paths the user can traverse. `systemctl`, `journalctl`, `findmnt`, `stat`, `getfacl`, or build reads can still be denied by local policy; those rows must remain `BLOCKED`, not inferred as PASS.

Protected state and logs may need supplemental read-only acquisition by an already authorized administrator: `/var/lib/athena-alpha-m1`, the ext4 fixture, canary receipt, backup metadata; `/var/log/athena-alpha-m1`; `/var/tmp/athena-d67-ext4/athena-d6-8yRDjc`; and complete process/cgroup or system-journal details on systems configured with restricted `/proc` or journal access. The collector itself must not be run as root or through `sudo`. In an existing authorized root console, collect only the supplemental evidence listed below into a separate protected evidence location, using reviewed read-only commands; do not run CTest or execute workspace/build binaries as root. If that access is unavailable, preserve `BLOCKED` results.

Example root-console supplemental commands (run only where approved; redirect output only to a separately prepared evidence directory):

```bash
date -u '+%Y-%m-%dT%H:%M:%SZ'
systemctl --failed --no-pager --plain --full
systemctl list-units --all --no-legend --no-pager --plain --full 'athena-alpha-m1-*.service'
for unit in $(systemctl list-units --all --no-legend --plain 'athena-alpha-m1-*.service' | awk '{print $1}'); do
  systemctl show "$unit" --property=Id,LoadState,ActiveState,SubState,MainPID,ControlGroup,Result,ExecMainCode,ExecMainStatus
  journalctl --no-pager -n 100 -o short-iso-precise -u "$unit"
done
ps -eo pid=,ppid=,uid=,stat=,comm=,args= --sort=pid
findmnt -T /var/lib/athena-alpha-m1/ext4-fixture -o TARGET,SOURCE,FSTYPE,OPTIONS
findmnt -T /var/tmp/athena-d67-ext4/athena-d6-8yRDjc -o TARGET,SOURCE,FSTYPE,OPTIONS
findmnt -T /opt/athena-qualification/build -o TARGET,SOURCE,FSTYPE,OPTIONS
stat -c 'type=%F mode=%a uid=%u gid=%g links=%h path=%n' /var/lib/athena-alpha-m1/ext4-fixture /var/tmp/athena-d67-ext4/athena-d6-8yRDjc
getfacl -cp /var/lib/athena-alpha-m1/ext4-fixture /var/tmp/athena-d67-ext4/athena-d6-8yRDjc
cat /sys/fs/cgroup/cgroup.events
```

These are direct read commands, not a complete cgroup descendant proof. Preserve any incomplete result as `BLOCKED`; never infer absence from `MainPID=0` or empty `ControlGroup`.

Root access does not make missing or ambiguous evidence a PASS. In particular, a zero `MainPID`, empty/missing `ControlGroup`, or unavailable `cgroup.events` does not prove that descendants are absent. Correlate the unit properties, per-process PID/PPID/cgroup capture, and `cgroup.events`; stop for administrator review if any part is ambiguous.

## Review outcomes

The script checks the previously reported count of 15 failed Alpha M1 units, captures each matching unit's state properties and recent journal, captures process PID/PPID/UID/command/cgroup observations, and reads relevant cgroup events. It records mounts, filesystem type/options, owner/group/mode, ACL output, names and metadata for protected directories, receipt/backup metadata hashes, and available space. It hashes the installer, manifest, runner, gateway support, canary, and sudoers draft against the fixed Session 34B baseline embedded in the script.

For protected and isolated builds it records CMake metadata, generated-file hashes, source path and a read-only Git HEAD observation when available, and manifest-pinned hashes for the fixed artifact allowlist. The 18 privileged names come from the fixed-hash Session 34B manifest. The collector does not invoke CTest or parse generated CTest scripts because those are untrusted executable CMake language. Actual current build inventory remains `NOT RUN`. Build cache/revision/hash agreement is evidence, not cryptographic source-to-binary provenance; the provenance gate remains `BLOCKED` unless the independently reviewed build process supplies that proof.

Treat any `FAIL`, `BLOCKED`, unexpected unit/process/cgroup, hash or inventory mismatch, inaccessible fixture/D6/receipt/backup path, symlink, or inconsistent observation as a stop. The report is evidence for administrative review only. It grants no staging, canary, installation, privileged-campaign, or rollback approval and does not qualify Alpha M1.

## Non-privileged regression tests

From the repository root:

```bash
bash -n qualification/alpha-m1-vm-evidence-collector.sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v tests.alpha_m1_vm_evidence_collector_test
```

These tests inspect the script, verify fixed baseline digests, exercise help and early rejection of symlinked or group-writable output/candidate directories, assert root refusal and container-prefix classification, validate the 18-name manifest entries, and run the report-header helper against temporary PATH sentinels. They do not run a collection against the development container or execute any privileged test.
