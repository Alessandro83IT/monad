# Alpha M1 Session 35 — security review

## Disposition

**BLOCKED — security does not approve the exact VM evidence for staging.** The observed environment is an isolated container, not the approved Debian qualification VM. System-scope systemd state, the 15 failed units, their descendant processes/cgroups, and protected fixture contents could not be observed. Absence of residual processes is therefore **not demonstrated**. No process, service, cgroup, mount, or protected file was changed.

This result concerns administrative readiness only. It is not Alpha M1 qualification. The exact candidate identities supplied for Session 34B were independently rehashed in the checkout and match the approved baseline:

| Artifact | SHA-256 | Result |
|---|---|---|
| Installer | `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7` | PASS, checkout bytes match baseline |
| Manifest | `00fd4036bf0f3873f5c2a0e932c912b4709460fa52c35a1c2564016a92811262` | PASS, checkout bytes match baseline |
| Runner | `c68bca67a7a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` | PASS, checkout bytes match baseline |
| Gateway support | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` | PASS, checkout bytes match baseline |
| Canary | `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` | PASS, checkout bytes match baseline |
| Sudoers draft | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` | PASS, checkout bytes match prior reviewed draft |

These hashes identify local checkout artifacts only. They do not establish which bytes, if any, are installed on the target VM.

## Environment and reproducibility

Read-only commands and results:

| Check | Command or method | Result / evidence |
|---|---|---|
| Repository baseline | `git status --short --branch`; `git rev-parse HEAD` | Branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`; numerous pre-existing modified/untracked paths. No existing work was reverted. |
| Identity | `id` | `uid=1000(alessandro) gid=1000(alessandro)`, no root privileges. |
| OS/kernel/architecture | `cat /etc/os-release`; `uname -a` | Debian GNU/Linux 13.7; Linux `6.12.113+deb13-amd64`; x86_64. |
| Virtualization | `systemd-detect-virt` | `container-other`. This is a container observation, not the Debian qualification VM. |
| Machine/boot IDs | `cat /etc/machine-id`; `cat /proc/sys/kernel/random/boot_id` | Machine ID `9bdbfd8678b5467cb85e4d8aa5e01fea`; boot ID `3fec68c2-2d94-45ea-b574-00249796f987`. Container namespace IDs; not accepted as VM identity. |
| UTC time | `date -u +%FT%TZ` | `2026-10-21T11:23:50Z` at collection. |
| Snapshot | No authorized hypervisor/snapshot interface was available in this container | **NOT RUN / unavailable**; no snapshot ID asserted. |
| Systemd | `systemctl list-units --all --no-legend 'athena-alpha-m1-*.service'`; `systemctl list-unit-files ...`; `systemctl show ...`; `systemctl is-system-running` | **BLOCKED**: each system-scope query failed to connect to the system bus (`Operation not permitted`). Unit states and systemd version/state were not established. |
| Relevant journal | `journalctl -u 'athena-alpha-m1-*' --no-pager -n 100` | **BLOCKED**: unit filter returned `No data available`; journal also reported that other users' messages are not visible. Not evidence of empty logs. |

## Units, processes, and cgroups

`ps -eo pid,ppid,user,stat,cgroup:50,comm --sort=pid` showed only PID 1 (`codex`) and the short-lived `ps` process in the current process namespace. `/proc/self/cgroup` identified this process as `0::/user.slice/user-1000.slice/session-4.scope`. This namespace does not expose a complete host process tree.

The mounted cgroup view is cgroup v2 and read-only. A search for `athena-alpha-m1` in `/sys/fs/cgroup/system.slice` returned no matching path in the visible container view. `cgroup.events` was readable for visible system/user slices, but the root `/sys/fs/cgroup/cgroup.events` path was absent; the visible system slice reported `populated 1`. Those observations cannot be mapped to any of the target VM's 15 failed units or their control groups.

| Required evidence | Status | Limitation |
|---|---|---|
| Full inventory and `ActiveState`, `SubState`, `MainPID`, `ControlGroup`, `Result` for all 15 units | **BLOCKED** | system bus unavailable; no authoritative host unit data |
| PID/PPID descendants and cgroup membership for each unit | **BLOCKED** | current process namespace is container-local; target unit cgroups not visible |
| Per-unit `cgroup.events` populated state | **BLOCKED** | unit cgroups not visible; no negative inference from `MainPID=0` or empty control-group data |
| Relevant complete system journal | **BLOCKED** | system journal access/filter unavailable |
| No residual processes | **BLOCKED gate** | cannot demonstrate absence; no process termination attempted |

No `systemctl reset-failed`, restart, stop, kill, or cgroup write was performed.

## Filesystem, staging, backups, and D6 preservation

`findmnt -rn -o TARGET,SOURCE,FSTYPE,OPTIONS` showed the container root on `/dev/sda2` as ext4 and read-only in the root namespace, with the checkout bind-mounted read-write beneath it. `/sys/fs/cgroup` is mounted cgroup2 read-only. These are container mount observations and do not establish the target VM mount table or options.

Read-only metadata probes returned:

| Path | Visible observation | Result |
|---|---|---|
| `/var/lib/athena-alpha-m1` | directory, mode `0700`, owner/group mapped to `nobody:nogroup` | Parent visible; contents inaccessible |
| `/var/lib/athena-alpha-m1/ext4-fixture` | `stat`, `namei`, `getfacl`, `df` denied | **BLOCKED**; fixture identity, ownership/mode/ACL, symlinks, mount and contents unknown |
| `/var/tmp/athena-d67-ext4` | directory, mode `0700`, owner/group mapped to `nobody:nogroup` | Parent visible; contents inaccessible |
| `/var/tmp/athena-d67-ext4/athena-d6-8yRDjc` | `stat`, `namei`, `getfacl` denied | **BLOCKED**; D6 residual identity/content/metadata unknown |
| `/opt/athena-qualification` and `/opt/athena-qualification/build` | directories, mode `0550`, mapped owner/group `nobody:nogroup` | Visible in this container; not sufficient proof of target VM state |
| `/tmp/monad-s32-verification-final4-20261021` | directory, mode `0775`, owner/group `1000:1000` | Visible in this container only |

`df -hT` could report the containing `/var/tmp` filesystem (`/dev/sda2`, ext4, 95G available at collection), but fixture-specific `df` was denied. `find` through both protected roots failed with permission denied. `getfacl` could inspect only the visible parents, not the protected targets. Consequently ACLs, symlink state, ext4 filesystem identity, fixture emptiness, receipt/archive/backup/log inventory, and free space on the fixture filesystem are **BLOCKED**, not PASS. The evidence paths were not opened for writing, renamed, moved, repaired, chmodded, or over-mounted.

## Privilege and path-boundary observations

The draft `qualification/athena-alpha-m1-sudoers` was inspected as a local checkout file. Its SHA-256 is `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18`. It lists 18 explicit runner invocations, each with a fixed test argument, and grants `alessandro ALL=(root) NOPASSWD: ATHENA_ALPHA_M1`. This is a static observation consistent with the previously reviewed draft, **not validation of installed sudoers syntax or live effective privilege**.

The container-visible `/etc/sudoers.d/athena-alpha-m1` was not readable as uid 1000; `visudo` was not run. The candidate draft cannot be treated as installed or validated. No `sudo` command was invoked. No claim is made here that live installed paths, ownership, mode, ACL, symlink resistance, runner hash binding, receipt handling, transaction lock, incomplete-transaction rejection, or rollback authorization are correct on the target.

The Session 34 and 34B security reports in the checkout describe prior static review of candidate transaction/receipt behavior and pin closure. Their static conclusions remain bounded to the named artifact hashes and isolated non-privileged tests; they do not close the VM-specific observations blocked above. Session 34B explicitly records the multi-file publication as individually atomic renames (not one atomic update), partial-install/manual-recovery behavior, and unsigned approval data requiring an independent human approval record.

## Required next evidence and security decision

**Security approval for staging: NOT GRANTED; gate BLOCKED.** Repeat the fixed read-only collection through the authorized interface on the dedicated Debian VM with sufficient read access to system scope, `/proc`, cgroups, system journal, and protected metadata. Record exact VM/hypervisor and snapshot identity from an authorized source. For each of the 15 failed units, establish the unit properties, inspect its complete cgroup process membership and `cgroup.events`, and correlate with the process tree. Inspect fixture/D6 metadata and mounts without traversing or changing inaccessible content until authorized access is available. If any descendant exists or absence cannot be established, retain the BLOCKED gate and escalate to the administrator; do not stop/reset units as part of this review.

Even if these read-only gates pass later, staging, live canary, installation, privileged campaign, and rollback each require their own explicit approval. This report grants none of them.
