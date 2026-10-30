# Monad Alpha M1 — Session 37: Qualification Evidence Hardening

**Disposition: BLOCKED. Alpha M1 is not qualified and installation is not authorized.** This session hardened the read-only evidence collector, added a static CTest inventory verifier, and prepared a VM-side acquisition procedure. It did not acquire new VM evidence or execute privileged operations.

## Scope and initial state

The reviewed historical capture is `/home/alessandro/monad-alpha-m1-evidence/alpha-m1-vm-evidence-20261021T145020Z-86810`. Its baseline remains **109 PASS, 34 BLOCKED, 7 NOT RUN** (150 controls). No Session 37 collector or CTest inventory acquisition was run in this container. Therefore the historical BLOCKED controls remain unresolved, even where the collector defect that generated a control has since been fixed.

Before edits, branch was `main`, HEAD was `abd6bac4b368427b76f6c98522afde928b418939`. The worktree already contained extensive modified and untracked project files, including the requested collector, tests, qualification documents, build directories, and evidence. Those were treated as existing work; no reset, cleanup, or overwrite was performed. The Session 34B candidate manifest and six pinned artifacts were not modified.

The Session 35 evidence directory remains byte-identical under the recorded tree digest procedure: 149 files, aggregate SHA-256 `f229f1b367f31053a283f564b3d2fa3edd713bfa1ede5134c899466f1bb8f67f`. This is a local integrity check of the historical directory, not an external attestation.

## Implemented hardening

- Systemd inventory now uses `systemctl list-units --all --type=service --output=json --no-pager` and `systemctl list-unit-files --type=service --output=json --no-pager`. The parser validates JSON structure, required fields, names, duplicates, and enums; it distinguishes failed, active, inactive, not-loaded, and transitional/maintenance states. Errors remain BLOCKED; zero parsed units are not interpreted as zero failures.
- The current 15 Alpha unit IDs are compared with the fixed Session 35 set. A process-cgroup audit scans the captured `/proc` records, blocks on unreadable or malformed records, and fails on Alpha service cgroup membership not accounted for by the current unit list, including stale units. No unit/process action is performed.
- The new CTest helper statically parses a narrow literal subset of generated `CTestTestfile.cmake`; it never invokes CMake or CTest. It requires a build path exactly matching one of the two manifest entries and compares complete ordered test names/counts, commands and arguments, privileged classification, and `LABELS`, `WORKING_DIRECTORY`, `TIMEOUT`, `SKIP_RETURN_CODE`, and `RUN_SERIAL`. Unknown properties and unsupported syntax (including nested `subdirs(...)`) remain BLOCKED. Thus the report does not assume root-file parsing proves a complete nested inventory.
- The collector pins helper SHA-256 `1d95eb5c781f4f21581a01a2b778e097535221af3b1a9b7610ad81cf576c87d1`, opens it once with `O_NOFOLLOW`, requires a stable bounded regular file, hashes it, and executes those exact verified bytes. The expected and observed helper digests are recorded. All inline Python interpreters use isolated mode (`python3 -I -`) to prevent current-directory module shadowing.
- The collector continues to refuse root, rejects malformed arguments and unsafe paths, writes only to a new private evidence directory, and does not run privileged tests, canaries, installation, rollback, cleanup, or system mutations.

## Verification performed in the container

Commands run after integration:

```text
bash -n qualification/alpha-m1-vm-evidence-collector.sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v tests.alpha_m1_vm_evidence_collector_test
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v tests.alpha_m1_ctest_inventory_test
```

Results: Bash syntax PASS; collector suite **24/24 PASS**; CTest inventory helper suite **18/18 PASS**. These tests exercise production parser/audit/helper functions, malformed and incomplete inputs, simulated D-Bus errors, symlink/path safety, root refusal logic, helper replacement rejection, stale cgroup detection, inventory substitutions, property changes, duplicates, and unsupported CMake syntax. They do not run the collector acquisition, CTest, canaries, or privileged tests. The CTest fixtures are simulated; no VM-generated inventory has been verified.

## Assurance levels

| Level | What is established | Status |
|---|---|---|
| A — Integrity | The six Session 34B candidate hashes still match their fixed baseline values; historical capture reported 7/7 allowlisted artifacts per build matching its manifest. | Candidate pin integrity PASS; build artifact integrity remains historical evidence only. |
| B — Coherence | Collector and helper now implement stricter checks for live CTest names/order/count/commands/properties and build metadata. The helper has only been exercised on synthetic test inputs in this session; no current VM build file was read. The reported 140 vs 147 total-test discrepancy remains unexplained. | BLOCKED pending approved VM-side generated-file verification; unsupported nested inventories are explicitly BLOCKED. |
| C — Provenance | No authenticated source-tree → build process → binary attestation was obtained. Hash equality, CMake cache, and observed HEAD do not prove lineage. | BLOCKED for both builds. |

A-level hash matches do not imply B-level consistency or C-level provenance.

## Gate matrix

| Gate | Session 37 disposition | Evidence / remaining work |
|---|---|---|
| VM and snapshot identity | BLOCKED | Session 35 observed guest virtualization type only; snapshot UUID was not captured. Verify VM and snapshot from the VirtualBox host and bind to guest machine-id/boot-id. |
| Systemd inventory and health | BLOCKED | Historical health was `degraded`; 15 units were reported failed. New JSON parsing is code-tested only; acquire and review fresh raw JSON on the authenticated VM. |
| Unit IDs and residual processes/cgroups | BLOCKED | Historical `ControlGroup=` values were empty and cgroup events unavailable. New historical-ID and stale-membership checks have not run on the VM. |
| Fixture, receipt, backups, logs | BLOCKED | Historical accesses were denied; those denials do not establish absence or validity. Perform separately approved, sequential no-follow metadata/read scopes. |
| D6 residual preservation | BLOCKED | Historical residual could not be traversed. Keep it untouched; acquire approved metadata-only evidence. |
| Generated CTest inventory | BLOCKED | Manifest declares the expected 18 privileged names and full inventories; generated VM files were not read. Run the pinned static helper through the collector on both manifest-fixed build paths; no CTest invocation. |
| Candidate Session 34B byte integrity | PASS (local recheck) | All six hashes below match the initial pins. This is integrity only. |
| Build binary integrity | PASS (historical only) | Session 35 captured 7/7 allowlisted artifacts matching pins for each build. No new build artifacts were read. |
| Source-to-binary provenance | BLOCKED | Neither protected nor isolated build has authenticated source/build/output linkage. Obtain a trusted attestation or separately review a reproducible isolated build; do not infer provenance. |
| Privileged campaign, canary, install, rollback, cleanup | NOT RUN | Deliberately not performed; separate approvals and all prerequisite gates remain outstanding. |

## Historical matrix of all 34 BLOCKED controls

The following matrix preserves the Session 35 control IDs and their evidence/cause. All 34 remain BLOCKED for Session 37 because no fresh VM collection was performed. The collector-side corrections for the failed-unit query do not rewrite historical evidence.

## Matrix of all 34 BLOCKED controls

Evidence paths below are relative to the evidence directory stated above. Rows grouped as “15 controls” list every individual control; each remains independently BLOCKED.

| # | Control | Cause and evidence | Read-only resolution / classification |
|---:|---|---|---|
| 1 | `vm-systemd-version` | `vm-systemd-version.txt`: `systemd` executable not found; other systemd tools did run. | Query `systemctl --version` or approved `/usr/bin/systemd --version`; collector probe/tool availability, not proof systemd is absent. |
| 2 | `vm-systemd-health` | `vm-systemd-health.txt`: `degraded` (exit 1). | On the identity-verified VM, inspect failed units and manager state; actual captured environment condition, cause not yet diagnosed. |
| 3 | `vm-snapshot` | `vm-snapshot.txt`: snapshot not queried. | Host-side VirtualBox identity and snapshot observation; deliberately omitted, requires host operator. |
| 4 | `systemd-alpha-failed-units` | `systemd-alpha-failed-units.txt`: historical Session 35 query error. Session 37 replaces it with machine-readable JSON inventory commands; no new VM capture has run. | Collector defect corrected in code; retain this historical control BLOCKED until an approved VM-side recapture and review. |
| 5 | `alpha-failed-unit-count` | `alpha-failed-unit-names.txt` is empty because the historical query failed; raw inventory listed 15 units. | Recompute from corrected structured result during approved VM recapture; historical count remains BLOCKED. |
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


## Candidate hashes and final implementation hashes

The six candidate Session 34B artifacts are unchanged and still hash to:

| Candidate | SHA-256 |
|---|---|
| `install-athena-alpha-m1-admin.sh` | `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7` |
| `alpha-m1-build-reconciliation-session34b-candidate.json` | `00fd4036bf0f3873f5c2a0e932c912b4709460fa52c35a1c2564016a92811262` |
| `athena-alpha-m1-runner.py` | `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` |
| `gateway_support.py` | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` |
| `athena-alpha-m1-systemd-canary.py` | `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` |
| `athena-alpha-m1-sudoers` | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` |

Final collector SHA-256: `6527d34cbfcca75244a77a1457f13e3d8e1b79aee5393f348c73bc96d74a74d2`.

CTest helper SHA-256: `1d95eb5c781f4f21581a01a2b778e097535221af3b1a9b7610ad81cf576c87d1`.

## Independent reviews

- **Core:** implementation completed; Bash syntax and collector suite reported PASS.
- **Verification: APPROVE.** Independently reviewed the final integrated collector/helper/procedure/tests and reported Bash syntax PASS plus focused suites PASS 42/42. No collector or CTest was run. The reviewer confirms `subdirs(...)` remains conservatively BLOCKED because Session 35 has no raw VM CTest files.
- **Security: APPROVE** at final collector SHA `6527d34cbfcca75244a77a1457f13e3d8e1b79aee5393f348c73bc96d74a74d2` and helper SHA `1d95eb5c781f4f21581a01a2b778e097535221af3b1a9b7610ad81cf576c87d1`. Approval covers the read-only evidence workflow only; it does not qualify Alpha M1 or authorize administrative actions.
- **Operations:** VM procedure prepared and updated to require sequential human approval/review after `namei`, stop on unexpected metadata, and match the collector’s JSON/helper-hash outputs. Not executed.

## Next manual VM acquisition

After human approval identifies the exact Debian VM/snapshot, reviewed checkout, and existing private evidence parent, execute only on that VM as the designated non-root user. Follow [the reviewed VM procedure](alpha-m1-session-37-vm-procedure.md), especially its sequential stop/review rules for protected paths. The collector invocation is:

```sh
cd /absolute/reviewed/checkout
test "$(pwd -P)" = /absolute/reviewed/checkout
test -d /absolute/private/evidence-parent
bash qualification/alpha-m1-vm-evidence-collector.sh \
  --output-parent /absolute/private/evidence-parent \
  --candidate-dir /absolute/reviewed/checkout/qualification
```

Replace placeholders only with approved literal paths. Review the new evidence directory and both `build-*-ctest-inventory.json` reports plus `build-*-ctest-helper-sha256.txt` sidecars. If the helper reports BLOCKED (including unsupported nested `subdirs(...)`), preserve that status and review a separately scoped read-only method. Do not run CTest, canaries, tests, installer, rollback, cleanup, or system mutations as part of this acquisition.

## Actions not performed

No collector acquisition, CTest/CTest test, canary, install, rollback, cleanup, `sudo`, privilege escalation, privileged read, service/cgroup/mount mutation, commit, or push was performed. The historical evidence and Session 34B candidate remain preserved. Alpha M1 remains **BLOCKED**, and installation is not authorized.
