# Alpha M1 Session 35 — integration report

## Decision

**BLOCKED.** Session 35 did not establish readiness for staging approval. The candidate identities and non-privileged regression checks passed in the accessible checkout, but the dedicated Debian qualification VM could not be identified or inspected through this container. In particular, absence of residual processes in the 15 previously failed `athena-alpha-m1-*.service` units is not demonstrated, the required protected-build provenance is not established, and Security did not approve the exact VM evidence.

This is an administrative-readiness decision, not a claim that the candidate failed its local checks and not a claim of Alpha M1 qualification. No installation, live canary, rollback, privileged test, cleanup, systemd change, mount change, commit, or push was performed.

## Environment and evidence boundary

| Check | Method / evidence | Status | Limit |
|---|---|---|---|
| Repository baseline | `git status --short --branch`; `git rev-parse HEAD` | PASS: `main`, `abd6bac4b368427b76f6c98522afde928b418939` | Pre-existing edits, untracked evidence and build directories were preserved; repository state is not a VM identity. |
| OS/kernel/architecture | `cat /etc/os-release`; `uname -a`; `uname -m` | PASS for container: Debian 13.7, kernel `6.12.113+deb13-amd64`, x86_64 | `systemd-detect-virt` returned `container-other`; not the qualification VM. |
| Machine and boot IDs | `/etc/machine-id`; `/proc/sys/kernel/random/boot_id` | PASS for container-visible IDs: `9bdbfd8678b5467cb85e4d8aa5e01fea`; `3fec68c2-2d94-45ea-b574-00249796f987` | Not accepted as VM identity. |
| Hypervisor and snapshot | `systemd-detect-virt`; no authorized snapshot interface available | BLOCKED / NOT OBSERVED | `systemd-detect-virt --vm` returned `oracle`, which does not establish VM identity; no snapshot ID is asserted. |
| systemd | `systemd --version`; `systemctl is-system-running` | BLOCKED | `systemd` executable was unavailable in the coordinator's repeat; system bus request failed with `Operation not permitted`. Verification report recorded a version string, but that result could not be independently reproduced and is explicitly treated as unverified. |
| UTC capture | `date -u +%Y-%m-%dT%H:%M:%SZ` | PASS for container observation | `2026-10-21T11:23:48Z`; this is not a VM capture timestamp. |

## Integrated findings

| Area | Result | Evidence and scope |
|---|---|---|
| Session 34B candidate and pins | PASS in checkout | Core and Verification independently matched the five supplied digests; Sudoers draft digest also matched its recorded installer pin. The digests identify checkout files, not installed VM files. |
| Installer source and syntax | PASS in checkout | `bash -n` passed. Read-only review found no automatic rollback; rollback is a separate approval-bound action; transaction locks and incomplete-transaction rejection are present; receipt/path checks are present. Source inspection and unprivileged tests do not establish live root ownership, crash durability, or VM state. |
| Non-privileged Session 34B regression | PASS | `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v tests.alpha_m1_admin_static_test tests.alpha_m1_installer_transaction_test tests.alpha_m1_receipt_path_test`: 25 PASS, 0 FAIL, 0 SKIP, 0 TIMEOUT. `bash -n` also PASS. |
| Build availability/inventory | PASS in accessible environment | `ctest --show-only=json-v1`: protected build 140 total / 18 privileged; isolated build 147 total / 18 privileged. No CTest cases were run. |
| Exact privileged inventory | PASS as inventory only | Both builds reported the same 18 labels, listed in the Verification report. None of the 18 tests ran. |
| Build source provenance | BLOCKED | Protected cache source is `/opt/athena-qualification`, without authenticated revision/input-to-binary proof. Isolated cache points to this checkout but does not bind binaries to current HEAD or candidate source. Do not run the privileged campaign from either build on this evidence. |
| Systemd units, processes, cgroups, logs | BLOCKED gate | Container PID/cgroup namespace and unavailable system bus do not expose the target VM. No evidence demonstrates descendant absence for the 15 failed units. `MainPID=0`, empty ControlGroup, or absent visible cgroup paths are not accepted as proof. No process/unit state was changed. |
| Filesystem, fixture, staging, backups, receipts, D6 | BLOCKED | Container-visible protected paths are inaccessible and have `nobody:nogroup` mapped ownership; this cannot establish VM ownership, ACL, symlink, mount, ext4 fixture, receipt/archive or backup state. D6 and fixture contents were not changed or represented as inspected. |
| Security approval | NOT GRANTED; BLOCKED | Security's independent report rejects staging readiness because the exact VM evidence is not observable and descendant absence is unproven. |

The exact privileged inventory recorded by Verification is:

```text
activation_execution_privileged_sources_test
activation_execution_privileged_legacy_test
activation_execution_privileged_owner_test
activation_execution_privileged_bind_test
activation_execution_privileged_nested_test
activation_execution_privileged_ext4_test
activation_plan_retention_privileged_owner_test
activation_plan_retention_privileged_bind_test
activation_plan_retention_privileged_nested_test
activation_plan_retention_privileged_ext4_test
activation_plan_persistence_privileged_test
legacy_d3_scanner_test
activation_plan_persistence_privileged_owner_test
activation_plan_persistence_privileged_bind_test
activation_plan_persistence_privileged_nested_test
activation_plan_persistence_privileged_tmpfs_test
activation_plan_persistence_privileged_ext4_test
initial_activation_migration_test
```

## Candidate identities

These checkout hashes match the fixed Session 34B baseline and were not changed:

| Artifact | SHA-256 |
|---|---|
| Installer | `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7` |
| Manifest | `00fd4036bf0f3873f5c2a0e932c912b4709460fa52c35a1c2564016a92811262` |
| Runner | `c68bca67a7a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` |
| Gateway support | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` |
| Canary | `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` |
| Sudoers draft (additional pinned artifact) | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` |

Manifest digest is the SHA-256 of the manifest file. Installed VM artifacts and protected build binaries were not read or authenticated here.

## Runbook and approval boundaries

`docs/qualification/alpha-m1-session-34-admin-runbook.md` now records the Session 34B pin closure and removes the obsolete open claims about automatic rollback and unreconciled pins. It retains the multi-file rename limitation, unsigned approval-token limitation, and all VM-side gates. It requires separate decisions for:

1. Read-only administrative verification.
2. Staging approval.
3. Real canary approval.
4. Installation approval.
5. Privileged campaign approval.
6. Any rollback approval.

No prior decision implies a later one. This session grants none of them.

## Required next evidence

The BLOCKED decision can be revisited only after an authorized read-only acquisition on the dedicated VM establishes VM/hypervisor/snapshot identity; complete systemd unit properties, logs, process descendants and relevant cgroup membership/events; filesystem and fixture/D6 identity and metadata; receipt/backup/staging state; and exact installed artifact hashes. Build provenance requires a demonstrated source snapshot and artifact linkage. If the protected build provenance remains unprovable, preserve it and propose a separately reviewed reconstruction into a new isolated build directory; do not reconstruct automatically. Keep all privileged tests, live canary, installation and rollback unrun until their independent human approvals are recorded.

## Report set

- [Core report](alpha-m1-session-35-core-report.md)
- [Verification report](alpha-m1-session-35-verification-report.md)
- [Security report](alpha-m1-session-35-security-report.md)
- [Operations report](alpha-m1-session-35-operations-report.md)
- [Updated Session 34 runbook](alpha-m1-session-34-admin-runbook.md)

No Alpha M1 qualification claim is made. The current decision is **BLOCKED**, because required evidence is unavailable, not because a destructive or privileged check failed.
