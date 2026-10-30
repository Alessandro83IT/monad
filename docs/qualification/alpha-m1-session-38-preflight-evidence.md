# Session 38 — Preflight evidence record

**Acquisition timestamp:** 2026-10-21 16:07 UTC (UTC).  
**Origin:** Codex development container, working directory `/home/alessandro/Projects/monad`; this is not the Alpha M1 VM or its VirtualBox host.  
**Collection type:** local read-only observation. No VM, snapshot, fixture, receipt, backup, log, D6 payload, or build contents were acquired.

## Repository context

Commands:

```sh
pwd
git status --short --branch
git rev-parse HEAD
```

Observed: working directory `/home/alessandro/Projects/monad`; branch `main` tracking `origin/main`; HEAD `abd6bac4b368427b76f6c98522afde928b418939`. The working tree had many pre-existing modified and untracked files, including build directories, qualification documents, collector, tests, and evidence. No cleanup or reset was performed.

## Execution environment and VM availability

Commands:

```sh
hostname
id
uname -a
systemd-detect-virt
command -v VBoxManage
VBoxManage --version
VBoxManage list vms
VBoxManage list runningvms
```

Observed: hostname `debian`; user `alessandro`, uid/gid 1000; Debian 13 container kernel `6.12.113+deb13-amd64`; virtualization detection `container-other`. `VBoxManage` was not installed; version/VM-list attempts returned `command not found`. Therefore the host VM list, `Athena-Alpha-M1` current power state, VM UUID, and snapshot inventory are **not observed**. This is not evidence that the VM is stopped or unavailable on its actual host.

## Build path preflight

Commands:

```sh
test -d /opt/athena-qualification/build
stat -c 'type=%F mode=%a uid=%u gid=%g path=%n' /opt/athena-qualification/build
test -d /tmp/monad-s32-verification-final4-20261021
stat -c 'type=%F mode=%a uid=%u gid=%g path=%n' /tmp/monad-s32-verification-final4-20261021
```

Observed: both paths exist in this container. `/opt/athena-qualification/build` is a directory, mode `550`, uid/gid `65534`; `/tmp/monad-s32-verification-final4-20261021` is a directory, mode `775`, uid/gid `1000`. No files were read from either directory. These observations do not establish build availability, identity, contents, or provenance in the actual VM.

## Artifact integrity observations

SHA-256 observations from the current repository checkout:

| Artifact | SHA-256 |
|---|---|
| `qualification/alpha-m1-vm-evidence-collector.sh` | `6527d34cbfcca75244a77a1457f13e3d8e1b79aee5393f348c73bc96d74a74d2` |
| `qualification/alpha-m1-ctest-inventory.py` | `1d95eb5c781f4f21581a01a2b778e097535221af3b1a9b7610ad81cf576c87d1` |
| `qualification/install-athena-alpha-m1-admin.sh` | `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7` |
| `qualification/alpha-m1-build-reconciliation-session34b-candidate.json` | `00fd4036bf0f3873f5c2a0e932c912b4709460fa52c35a1c2564016a92811262` |
| `qualification/athena-alpha-m1-runner.py` | `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` |
| `qualification/gateway_support.py` | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` |
| `qualification/athena-alpha-m1-systemd-canary.py` | `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` |
| `qualification/athena-alpha-m1-sudoers` | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` |

These are local byte-integrity observations, not VM capture, transfer authorization, installed-file state, or build provenance.

## Historical evidence integrity

Historical directory: `/home/alessandro/monad-alpha-m1-evidence/alpha-m1-vm-evidence-20261021T145020Z-86810`. The same read-only tree digest procedure records 149 files and SHA-256 `f229f1b367f31053a283f564b3d2fa3edd713bfa1ede5134c899466f1bb8f67f`. Session 36/37 reports record the baseline 150 controls: 109 PASS, 34 BLOCKED, 7 NOT RUN. No historical file was modified.

## Limits and disposition

All observations above originated in the container or are prior historical evidence. No Alpha M1 VM or snapshot evidence was acquired. No collector, inventory helper, CTest, test case, privileged command, or protected guest-path read was executed. No VM or system state was changed. Treat VM availability and both real VM build paths as NOT OBSERVED; retain all 34 historical controls as BLOCKED.
