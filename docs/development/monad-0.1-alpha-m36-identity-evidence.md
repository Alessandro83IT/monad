# Monad 0.1 Alpha M36 — identity, mount baseline and evidence recovery

**Verdict: `BLOCKED_INSUFFICIENT_EVIDENCE`. Alpha M1 remains `NO-GO / NOT QUALIFIED`; privileged acquisition is not authorized.** This report records read-only observations from the current sandbox. It does not attest the VirtualBox host or approve the proposed mount baseline.

## 1. Executive summary

The local M34.1 collector matches its requested SHA-256. The current process observes Debian 13.7, Linux 6.12.113+deb13-amd64, x86_64, and VirtualBox DMI vendor/product strings. The machine ID differs from the collector's pinned machine ID, and DMI product UUID is unreadable. These observations do not identify the expected VM or snapshot. `VBoxManage` is unavailable, and no host attestation was acquired.

The five configured collector roots resolve in this process's mount namespace to the same observed root mount (ID 204, ext4, major/minor 8:2). The checkout is a separate subtree mount (ID 475) on the same device. This is a sandbox view; correspondence to the guest's future privileged process is unproven. The historical M32.1 directory exists according to path metadata, but is mode 0700 and owned by `nobody`; its contents cannot be read as this user. Existence is not integrity or provenance.

M35's reported administrative discrepancies remain secondary-source claims: runner digest `5d6ee…021d5` versus current installer pin `c68b…9def`, and support reported `REQUIRED_ABSENT` in M32.1 versus a current repository support file. Raw M32.1 and transaction evidence are unavailable here, so no cause is assigned and no reconciliation is performed.

## 2. Baseline, provenance and preflight

Before writing, Git reported branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`, tracking `origin/main`. The worktree already contained numerous modified tracked files and untracked reports, qualification material, build trees, caches and tests. They were preserved. The requested report path was absent. No other file was created or changed by this session.

| Item | Observation | Classification |
|---|---|---|
| `qualification/m34-1-targeted-forensic-acquisition.sh` | SHA-256 `56771879aa73bbdcac4368c9babc990f73150401b6aecfc8cb2646263c34d9d9` | `MATCH` to supplied pin for local bytes only |
| Host utilities | `findmnt`, `lsblk`, `python3` available; `VBoxManage`/`vboxmanage` unavailable | `OBSERVED` |
| Execution context | UID 1000, GID 1000; Debian Linux sandbox; no privilege requested or used | `OBSERVED` |
| Sandbox boundary | Workspace writable; `/tmp` read-only in observed namespace; administrative paths not writable; no escalation available in this session | `OBSERVED` |
| SSH context | SSH-related environment variable names exist; no SSH attempt or host configuration inspection performed | `UNKNOWN`; variables do not authorize or prove host access |

The four requested M35–M34 documents were read. Their claims are treated as reports from the workspace, not as independent raw evidence. No collector was executed and no administrative acquisition was attempted.

## 3. Guest identity

Status terms: `OBSERVED` means read in this process; `MATCH`/`MISMATCH` compare to a stated historical pin; `UNAVAILABLE` means access failed; `UNKNOWN` means no relevant evidence was available. VM and snapshot identity are separate from guest identity.

| Identifier | Observation | Comparison/status |
|---|---|---|
| Distribution | Debian GNU/Linux 13 (trixie), Debian version 13.7 | `OBSERVED`; not a unique identity pin |
| Kernel | `6.12.113+deb13-amd64` | `OBSERVED`; no historical kernel pin supplied |
| Architecture | `x86_64` | `OBSERVED`; no historical architecture pin supplied |
| Hostname | `debian` (`uname -n`) | `OBSERVED`, `MATCH` collector literal |
| machine-id | `9bdbfd8678b5467cb85e4d8aa5e01fea` | `OBSERVED`, `MATCH` collector literal |
| boot-id | `31579c3a-b457-4efb-82e2-ff0ab11d0c16` | `OBSERVED`; boot-scoped, no expected pin |
| DMI system vendor | `innotek GmbH` | `OBSERVED`, `MATCH` collector vendor condition |
| DMI product name | `VirtualBox` | `OBSERVED`; supports virtualization only |
| DMI product UUID | Read returned `Permission denied` | `UNAVAILABLE`; cannot compare with collector pin `91ea75e4-e972-5741-9ee6-583e9ae0462f` |
| DMI product serial | Not read in this session | `UNKNOWN`; collector expects `VirtualBox-e475ea91-72e9-4157-9ee6-583e9ae0462f` |
| Expected VirtualBox VM UUID | `e475ea91-72e9-4157-9ee6-583e9ae0462f` | Historical target pin only; guest observation is not host VM UUID evidence |
| Expected snapshot name / UUID | `Alpha-M1-Before-Privileged-Tests-20261020` / `12e7d712-0154-4127-9fb2-8dd848242877` | Historical target pins only; guest cannot establish current snapshot or presence |

The available machine-id matches the local collector's literal, but is not unique proof: guest values can be cloned, and no host-to-guest correlation was acquired. The product UUID is unavailable, so full collector identity is not established. No VM UUID, snapshot UUID or snapshot state is classified `MATCH`.

## 4. VirtualBox host correlation

No VirtualBox management executable is available in this environment. No authorized host API, supplied host-side record, or independent VM configuration evidence was present. SSH was not attempted. Host access is therefore `UNAVAILABLE` in this session and host attestation is **not established**.

Read-only host procedure for the Gentoo VM custodian (instructions only; not executed):

1. On the host console, identify the registered VM by UUID using the installed VirtualBox CLI, for example `VBoxManage showvminfo e475ea91-72e9-4157-9ee6-583e9ae0462f --machinereadable`. Record command path/version, UTC time, exit status, and unedited output. Confirm both exact UUID and name `Athena-Alpha-M1`.
2. Record `VBoxManage snapshot e475ea91-72e9-4157-9ee6-583e9ae0462f list --details` and `VBoxManage showvminfo ... --machinereadable`. Confirm exact snapshot name and UUID, presence in the tree, current snapshot pointer/state, and VM state. Stop on missing/duplicate/ambiguous identity or unexpected state; do not restore or change state.
3. From `showvminfo`, record the configuration file path and every attached virtual disk UUID, location, controller/port, and attachment state. Compare to a separately approved baseline/configuration record. A path or disk match alone does not prove content identity.
4. Preserve raw outputs and tool version in the approved evidence location. Hash each output with SHA-256; have the VM custodian and independent reviewer bind the evidence to the host and acquisition time. Record any differences from the approved baseline explicitly. Do not export or modify VM configuration as part of this read-only check.

This procedure requires host operator/custodian access but does not itself authorize a VM state change. Until its results are independently reviewed and correlated with guest evidence, status remains `UNKNOWN`, not `HOST_ATTESTED`.

## 5. Mount topology observed

Read-only parsing of `/proc/self/mountinfo` and `findmnt` was performed. Configured roots were correlated by longest matching mount point. Reported device `8:2` is also `st_dev=2050` in the path metadata. Administrative leaf contents were not opened.

| Collector root | Mount ID / parent | Major:minor; root; mount point | Filesystem/source; observed options | Path metadata / notes |
|---|---:|---|---|---|
| `/usr/local/sbin` | 204 / 555 | 8:2; `/`; `/` | ext4 `/dev/sda2`; `ro,nosuid,nodev,relatime`; optional `master:1` | Directory stat succeeded, mode 0755, UID/GID 65534. Resolves through root mount. |
| `/etc/sudoers.d` | 204 / 555 | 8:2; `/`; `/` | ext4 `/dev/sda2`; same root options | Directory stat succeeded, mode 0755, UID/GID 65534. Resolves through root mount. |
| `/root/athena-alpha-m1-stage` | 204 / 555 | 8:2; `/`; `/` | ext4 `/dev/sda2`; same root options | Metadata lookup denied at path; mount correlation comes from mountinfo only. |
| `/var/lib/athena-alpha-m1` | 204 / 555 | 8:2; `/`; `/` | ext4 `/dev/sda2`; same root options | Directory stat succeeded, mode 0700, UID/GID 65534. Contents not inspected. |
| `/var/lib/monad-m32-1-evidence-20261029T194120Z` | 204 / 555 | 8:2; `/`; `/` | ext4 `/dev/sda2`; same root options | Directory stat succeeded, mode 0700, UID/GID 65534. Contents inaccessible. |
| Checkout `/home/alessandro/Projects/monad` | 475 / 204 | 8:2; `/home/alessandro/Projects/monad`; checkout path | ext4 `/dev/sda2`; `rw,nosuid,nodev,relatime`; optional `master:1` | Distinct subtree/bind-like mount ID on same device; mode 0775, UID/GID 1000. |
| `/tmp` | 453 / 204 (nested writable mount 465 / 453) | 0:40; `/`; `/tmp` | tmpfs; outer observed `ro,nosuid,nodev`, nested `rw,nosuid,nodev` | Mount namespace exposes nested mounts; this session did not write there. |

No overlay was observed for the listed roots. `/proc/self/mountinfo` is authoritative only for this process's namespace. The workspace and sandbox policy make it unsafe to infer that it is the future guest process namespace: neither same namespace nor same topology was demonstrated. Mount IDs are namespace-specific observations and not portable expected pins.

## 6. Proposed mount baseline — NOT APPROVED

| Root | Proposed acceptance conditions | Reject conditions | Evidence and trust limit |
|---|---|---|---|
| `/usr/local/sbin` | Confirm on identified target guest; root is expected only if independently approved; record mount ID, parent, major/minor, root, type and ancestors immediately before acquisition; require stable descriptor mount ID | Unapproved separate/bind/overlay mount, changed ID, missing metadata, unexpected ancestor or access denial | Current namespace says root ID 204, ext4 8:2. Sandbox view, not target baseline. |
| `/etc/sudoers.d` | Same, and independently confirm approved administrative filesystem placement | Any unexplained distinct mount, identity change, overlay or inaccessible parent | Current namespace says root ID 204. Directory metadata is sandbox presented (UID 65534), not trustworthy target ownership. |
| `/root/athena-alpha-m1-stage` | Same; require path metadata from approved guest evidence without broadening collector scope | Unavailable path metadata, unexpected mount/type, or unexpected ancestor topology | Mount mapping only; direct stat was denied. |
| `/var/lib/athena-alpha-m1` | Same; review expected protected-state filesystem and mount ancestors | Any unapproved mount, namespace mismatch, changing ID, or boundary within configured root | Current namespace says root ID 204. Visible ownership/mode is sandbox translated and not target evidence. |
| M32.1 bundle path | Same; additionally require custodian-approved evidence storage and immutable/read-only source handling for any later verification | Inaccessible content, unapproved mount, mutation, or incomplete provenance | Current namespace says root ID 204; directory metadata is available but content is not. |

Before acceptance, operator and independent reviewer must specify expected filesystem/root and mount relationships, not merely bless observed accessibility. Require a full mountinfo record from the identified operational guest, parent/ancestor review, and a same-context descriptor-bound check immediately before any separately authorized acquisition. The present table is a proposal for review only; it does not approve initial mounts or change collector behavior.

## 7. M32.1 evidence availability and integrity

`stat` and `ls -ld` on `/var/lib/monad-m32-1-evidence-20261029T194120Z` succeeded. Observed metadata: directory, mode 0700, UID/GID 65534 (`nobody:nogroup`), device 8:2, inode 1703951, timestamps 2026-10-29 around 20:41 local time. `test -e` succeeded. Listing/opening contents as UID 1000 is denied by directory permissions. No privilege was used, no permission changed, and no content was copied.

| Check | Result |
|---|---|
| Path accessibility / existence | `OBSERVED_PRESENT`; content traversal `BLOCKED` by mode 0700 and ownership mismatch |
| Expected structure (`inventory.json`, `SHA256SUMS`) | `UNKNOWN`; names cannot be inspected |
| Checksums | `UNKNOWN`; neither checksum file nor covered files readable, so none verified |
| Inventory schema/coherence | `UNKNOWN`; no inventory bytes available |
| Historical acquisition identity, exit status, stdout/stderr and custody | `UNKNOWN`; not available from the bundle path metadata |

M35 reports that a previous narrative attributed schema 4, 598 entries, 1176 enumerated entries, zero bytes hashed and `PARTIAL`/`REQUIRED_ABSENT` to M32.1. This is `CORROBORATED` only as local report content, not verified against the raw bundle. Future authorized read-only verification must be performed by the evidence custodian or an already authorized administrator: preserve the source, record directory/file metadata and raw bytes into the approved evidence process without altering the source; independently hash transferred files; verify `SHA256SUMS`; validate schema and every inventory reference/count/status; preserve exact command/tool identity, exit status and custody metadata. Do not use this report as a substitute for the raw bundle.

## 8. Administrative discrepancies and evidence grades

| Subject | Existing report claim | Grade and limit |
|---|---|---|
| Installed runner | M35 recounts historical digest `5d6ee350…021d5` | `CORROBORATED` as M35 text only; installed bytes not available here |
| Installer runner pin | M35/M34 report current local installer pin `c68bca67…9def` | `VERIFIED` as reported local source hash only if independently rehashed in this session: not rehashed here; no installed installer linkage |
| Runner relationship | Reported installed digest differs from reported installer pin | `INFERRED` discrepancy from report values; neither source chain nor cause established |
| `gateway_support.py` | M35 recounts M32.1 `REQUIRED_ABSENT`; repository has a support implementation | `CORROBORATED` as report content; no raw inventory, installed file, or provenance comparison available here |
| Backup / transaction | Reports describe named backup `run-20261021-061921` and incomplete historical evidence | `UNKNOWN` as raw transaction state; no backup content inspected in this session |
| File provenance | Repeated report statements and local repository artifacts exist | `UNKNOWN`; repetition is not independent corroboration; no signed release/source-to-installed binding |
| Historical transaction | Reported marker/manifest state lacks raw transaction bytes and logs here | `BLOCKED`; no cause assigned, no reconciliation performed |

Use `VERIFIED` only for directly checked bytes/metadata in this session, `CORROBORATED` for claims repeated in available reports without independent raw evidence, `INFERRED` for a bounded conclusion from stated inputs, `UNKNOWN` where evidence is absent, and `BLOCKED` where access/authorization prevents a necessary check. No evidence here supports choosing between a different generation, incomplete installation, later change/removal, relocation, or reconstruction error.

## 9. Residual blocks

1. **Guest-to-VM/snapshot identity — BLOCKED.** Guest observations do not tie this process to VM UUID `e475…0462f` or snapshot UUID `12e7…42877`; product UUID is unreadable.
2. **Operational mount baseline — BLOCKED.** Observed sandbox namespace is not proven to match the privileged target process; no baseline has operator approval.
3. **Raw M32.1 integrity/schema — BLOCKED.** Bundle is present by metadata but contents are inaccessible; checksum and schema cannot be assessed.
4. **Administrative discrepancy/provenance — BLOCKED.** Runner, support, backup and transaction claims lack primary evidence and custody chain; causes remain unknown.
5. **Acquisition authorization and qualification — NOT GRANTED / NO-GO.** This session authorizes neither privileged reads nor any qualification campaign.

## 10. Minimum unlock sequence

| Step | Evidence and location | Acquirer / authorization | Integrity and pass criterion | Stop criterion |
|---|---|---|---|---|
| 1. Host identity | Raw read-only VirtualBox outputs and approved VM config/disk baseline from Gentoo host | VM custodian on host; ordinary host access, with operator approval for evidence handling | Preserve command/version/time/status; SHA-256 outputs; independent review confirms VM name+UUID, snapshot UUID/presence/current state, config path and disks agree with approved baseline | Missing/ambiguous VM or snapshot, unexpected state/config/disk, or no independent baseline |
| 2. Guest identity and mount baseline | Guest OS/identity fields and full mountinfo plus metadata for configured roots/ancestors from the identified running guest | Authorized guest operator; no identifiers changed. Any protected metadata access requires separately approved existing authority | Preserve raw outputs and hashes; correlate guest product identifiers with host record; reviewers explicitly approve expected root/type/device/mount relationships and same-process namespace | Unreadable required pin, identity mismatch, namespace correspondence unproven, unexplained mount or topology change |
| 3. M32.1 bundle | Existing `/var/lib/monad-m32-1-evidence-20261029T194120Z` on approved evidence filesystem | Evidence custodian or already authorized administrator; separately authorized read-only access only | Verify original checksums, inventory schema/references/counts, collector digest/run status and transfer custody independently | Any checksum/schema inconsistency, missing raw execution/custody evidence, source mutation, or need to change permissions |
| 4. Admin source comparison | Runner/support/sudoers targets, named backup transaction tree, installer/source and historical logs in their existing administrative locations | Evidence custodian/authorized administrator; bounded read-only scope approved by operator and security reviewer | Hash raw targets; compare against authenticated source/release and installer pin; validate transaction chronology/markers/payloads and custody; independent review explains or explicitly dispositions discrepancy | Incomplete scope, unauthenticated comparison source, unexplained contradiction or any proposed reconciliation/state change |
| 5. Authorization decision | Reviewed evidence set and exact read scope/output handling | Designated operator and qualification/security approvers | Explicit, separate written authorization naming guest, snapshot, bytes/scope, custody, destination and stop authority; this is only authorization review | Any unresolved critical identity, evidence integrity, mount or scope issue |

Steps 1–4 are evidence gathering/review, not qualification. Step 5 is a human decision. A subsequent privileged acquisition remains separately gated and is not authorized by `READY_FOR_AUTHORIZATION_REVIEW`.

## 11. GO / NO-GO criteria

- **Current state: NO-GO.** Identity correlation, mount acceptance, primary evidence integrity and provenance are unresolved.
- **Candidate for authorization review:** only after the identity, mount, bundle and administrative evidence above pass independent review and the operator accepts the exact scope. This verdict does not itself authorize acquisition.
- **Acquisition:** requires separate explicit authorization, exact reviewed collector bytes, target binding, approved destination and stop conditions. Any mismatch, inaccessible required evidence, unapproved mount, checksum/schema problem or provenance gap is a stop.
- **Alpha M1 qualification:** remains NO-GO / NOT QUALIFIED until the approved Debian VM campaign completes every required privileged case and the evidence review passes. Nothing in M36 satisfies that campaign.

## 12. Final verification and verdict

No `HOST_ATTESTED` claim is made. The observed guest fields are separated from VM and snapshot pins; the sole machine-id match is not treated as complete identity. The mount IDs are namespace observations, not approved pins. Bundle path presence is not confused with readable content or authenticated evidence. Reported discrepancies are not assigned causes. No privileged operation, collector run, mount change, snapshot operation, reconciliation, or test was performed. The prior checkout state was preserved.

**Final verdict: `BLOCKED_INSUFFICIENT_EVIDENCE`. Alpha M1 remains `NO-GO / NOT QUALIFIED`; privileged acquisition remains not authorized.**
