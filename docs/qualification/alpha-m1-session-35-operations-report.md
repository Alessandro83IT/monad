# Alpha M1 Session 35 — Operations report

## Scope and disposition

Operations updated the Session 34 administrative runbook for Session 35 and recorded the available environment and operational limits. No candidate artifact, VM state, installed administrative path, fixture, D6 residual, receipt, backup, log, or test was changed. No `sudo`, systemd mutation, mount operation, installation, canary, rollback, CTest case, or privileged test was run. No commit or push was made.

**Operations result: BLOCKED for administrative readiness.** This execution environment is a container, not the dedicated Debian qualification VM. The environment's Debian/kernel facts below identify the container only. VM identity, snapshot ID, VM-side administrative evidence, and staging are unavailable here. This report does not infer a snapshot ID or claim qualification.

## Environment identification

| Item | Result | Status / limitation |
|---|---|---|
| Collection time | `2026-10-21T11:23:53Z` from `date -u` | PASS for timestamp of this container observation |
| OS release | Debian GNU/Linux 13 (trixie), `DEBIAN_VERSION_FULL=13.7` from `/etc/os-release` | PASS for container-visible release; not VM evidence |
| Kernel / architecture | `Linux debian 6.12.113+deb13-amd64 #1 SMP PREEMPT_DYNAMIC Debian 6.12.113-1 (2026-10-15) x86_64`; `uname -m` = `x86_64` | PASS for container-visible values; not VM evidence |
| Virtualization | `systemd-detect-virt` returned `container-other` | PASS: explicitly identifies container environment |
| Machine ID / boot ID | `9bdbfd8678b5467cb85e4d8aa5e01fea` / `3fec68c2-2d94-45ea-b574-00249796f987` | PASS for container-visible IDs only; not VM identity |
| systemd | `systemd --version`: command not found; `systemctl is-system-running`: failed to connect to system scope bus, exit 1 | BLOCKED: systemd version/state of the target VM cannot be queried from this container |
| Snapshot identity | No authorized VM/hypervisor/snapshot-management interface was available or queried | BLOCKED / NOT OBSERVED; no snapshot identifier asserted |

Reproduction methods were read-only: `date -u`, `cat /etc/os-release`, `uname -a`, `uname -m`, `systemd --version`, `systemctl is-system-running`, `systemd-detect-virt`, `cat /etc/machine-id`, and `cat /proc/sys/kernel/random/boot_id`. The machine and boot IDs above must not be used to identify or authorize an operation on the qualification VM.

## Runbook update

Updated `docs/qualification/alpha-m1-session-34-admin-runbook.md` to record Session 34B pin closure and the exact reviewed baseline, remove the stale statements that the automatic rollback defect and pin mismatch were still open, and retain the known non-atomic multi-file publication and VM-side limits. The runbook now distinguishes these six independent gates: read-only administrative verification, staging, real canary, installation, privileged campaign, and any rollback. Each requires its own decision; none follows from another.

Session 34B closure evidence records installer SHA-256 `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7` and manifest SHA-256 `00fd4036bf0f3873f5c2a0e932c912b4709460fa52c35a1c2564016a92811262`; the runner, gateway support, and canary pins are recorded there as reconciled and reviewed. These are the reported candidate identities, not hashes freshly collected from a VM. The runbook leaves VM-side verification and each state-changing approval open.

## Operational procedures and current gates

The runbook requires an authorized read-only VM collection before any staging decision. The collection should capture Debian release, kernel/architecture, hypervisor and VM identity, machine/boot IDs, systemd version/state, UTC time, and snapshot identity only through an authorized interface. It also requires complete unit properties and logs, independent PID/PPID/cgroup correlation and `cgroup.events` where available, mount/filesystem and fixture/D6 metadata, protected and isolated build provenance, exact privileged-test inventory, and artifact/receipt/backup/staging metadata. Empty `MainPID`, `ControlGroup`, or absent cgroup files are not proof that descendants are gone.

The previous 15 failed unit records and D6/fixture/build observations remain historical, session-supplied VM evidence. They were not recollected in this operations task. No processes were terminated; no units reset/restarted; no cgroups or mounts changed. Preserve `/var/lib/athena-alpha-m1/ext4-fixture`, `/var/tmp/athena-d67-ext4`, and residual `athena-d6-8yRDjc` exactly in place.

| Gate | Operations status | Evidence / limitation |
|---|---|---|
| Read-only VM verification | BLOCKED | Only container identity was observed; target VM process, filesystem, build, receipt, and artifact evidence not collected |
| Staging | NOT RUN | No staging approved or created |
| Real canary | NOT RUN | No VM receipt or live canary |
| Installation | NOT RUN | No installation or administrative file change |
| Privileged campaign | NOT RUN | All 18 privileged cases remain not run |
| Rollback | NOT RUN | No transaction recovery or rollback action |

The Session 34B report records the final non-privileged regression as 25 PASS, 0 FAIL, 0 SKIP, 0 TIMEOUT and `bash -n` PASS. Operations did not rerun those checks. The two referenced CTest build inventories and their provenance were not inspected here; Session 34B records 140 tests / 18 privileged for `/opt/athena-qualification/build` but says source provenance is not authenticated to this checkout, and 147 / 18 for `/tmp/monad-s32-verification-final4-20261021` whose cache source path does not prove the binaries came from the candidate snapshot. This does not satisfy the protected-build provenance gate.

## Evidence identity and limits

The relevant baseline hashes are the Session 34B reported candidate values:

| Artifact | SHA-256 |
|---|---|
| Installer | `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7` |
| Manifest | `00fd4036bf0f3873f5c2a0e932c912b4709460fa52c35a1c2564016a92811262` |
| Runner | `c68bca67a7a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` |
| Gateway support | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` |
| Canary | `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` |

Operations did not recompute these candidate hashes in the VM, and they are not installed-file identities. The current repository is shared with other work: the pre-existing modified files and untracked evidence/build directories observed at task start were preserved. Repository revision was `abd6bac4b368427b76f6c98522afde928b418939` on branch `main` at task start.

The report and runbook describe the evidence scope and do not replace the required target-VM acquisition. No Alpha M1 qualification, recovery, or staging readiness is asserted.
