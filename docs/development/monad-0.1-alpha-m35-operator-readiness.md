# Monad 0.1 Alpha M35 — operator readiness, guest identity and mount topology

**Verdict: `BLOCKED_INSUFFICIENT_EVIDENCE`. Alpha M1 remains `NO-GO / NOT QUALIFIED`; privileged acquisition remains NOT AUTHORIZED.** This session verified the local M34.3 collector and ran synthetic tests only. The current guest does not match the production identity literals in that collector, no VirtualBox host attestation or approved Alpha snapshot identity is available, and the original M32.1 evidence bundle and administrative sources are not present as authenticated raw evidence.

## 1. Executive summary

The requested collector digest is correct: `qualification/m34-1-targeted-forensic-acquisition.sh` hashes to `56771879aa73bbdcac4368c9babc990f73150401b6aecfc8cb2646263c34d9d9`. Its M34.1, M34.3 and M34.4 synthetic suites pass here. The unchanged M34.2 historical test has one expected failure because it asserts the old false-`COMPLETE` behavior; three tests skip for unavailable deterministic I/O fault injection and prohibited mount creation. This does not weaken the M34.3 fix, but the old failing assertion must remain visible to reviewers.

The environment reports Debian 13.7, kernel `6.12.113+deb13-amd64`, x86_64, VirtualBox DMI vendor `innotek GmbH`, and product name `VirtualBox`. The machine ID digest was recorded without publishing the full value. DMI product UUID is unreadable. These are guest-visible observations only. The current host name and guest identifiers do not match M34.1's pinned production identity (`hostname=debian`, machine ID, product UUID and serial literals), so invoking M34.1 in production mode here would correctly block before acquisition. VirtualBox DMI strings do not prove the host VM UUID, snapshot, or intended qualification guest.

The five M34.1 configured roots map through this process's `/proc/self/mountinfo` to `/`, mount ID 455, parent ID 158, major/minor 8:2, ext4 `/dev/sda2`, mount root `/`. The current checkout is on mount ID 540 (same major/minor/device, a distinct subtree/bind mount); `/tmp` is tmpfs mount ID 491. The collector accepts the initial mount independently for each configured root and does not record those IDs in its manifest. Therefore this local topology observation cannot establish the intended Alpha guest topology or prove that an unexpected mount at a configured root would be rejected.

**Operational decision:** do not proceed to privileged acquisition from this environment. A future authorization review can occur only after an operator identifies the intended guest and snapshot, supplies independent host-side identity evidence, and makes the approved raw M32.1 and transaction evidence available with custody information. This is a gate decision, not authorization.

## 2. Baseline, scope and hashes

Preflight ran before other session activity. Initial Git state was branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`, tracking `origin/main`. The worktree already contained many modified tracked files, untracked reports, qualification artifacts, build directories, caches and evidence. All pre-existing state was preserved. Both authorized M35 output paths were absent. Only this report was created; no M35 test file was needed because the existing synthetic suites covered the requested cases.

The expected collector digest matched, so dynamic collector testing was allowed. No pin or baseline was changed.

| Artifact | SHA-256 observed in this session | Status / source limit |
|---|---|---|
| `qualification/m34-1-targeted-forensic-acquisition.sh` | `56771879aa73bbdcac4368c9babc990f73150401b6aecfc8cb2646263c34d9d9` | VERIFIED local bytes; matches requested digest. Not proof of installed or previously used bytes. |
| `qualification/m32-1-readonly-acquisition.sh` | `7d800b533b1ef8f42f3bc3e6b85d84c525742554e7ec7f51bf02c244250f8ee6` | VERIFIED local bytes; no claim about M32.1 acquisition invocation. |
| `tests/m34_1_targeted_forensic_acquisition_test.py` | `938254e4cd8cf53df0e1b4051c7a8febb6a70ac189733196f6d2351b9d94f4f8` | VERIFIED local bytes. |
| `tests/m34_2_security_audit_test.py` | `fbed6b9c38f456d166212b031f29b98259fff1746a411df7af3fd16d82fe0d79` | VERIFIED local bytes; unchanged historical assertion. |
| `tests/m34_3_security_regression_test.py` | `ea8183a31ead5e1ec20980d7d74d3eeb4ecd5dddf117a73a21f9dd6a69654fcb` | VERIFIED local bytes. |
| `tests/m34_4_independent_verification_test.py` | `f7efb8359b12ea54c0f78b3ff261542ffaf72b883c8d94671b241e52c384e833` | VERIFIED local bytes. |
| `qualification/install-athena-alpha-m1-admin.sh` | `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7` | Read-only consultation; local installer is untracked and not authenticated as installed/history. |

The six mandatory M33/M34 review documents were read. Their local hashes were measured: M33 `d5593040e52a0cc691705d047165eae1d6c9109d76239bbf002799579f9c4a96`; M34 `d549a54f6a7308a736cb7fb0680dab6014a78f0188b7003cf2cd1e94cbb054ed`; M34.1 `934ec1fcfa1344450e7308b10784e1c78253b124ef9edb81b07a7da17ca9a10a`; M34.2 `a159cb3ac08e14aa2c2e1c6a9a32b4ebbdaf526c2c536321cfce7f33169abe21`; M34.3 `baaa5e55a0d2254c408408f2815a56b329234b95380724803fc42e47d707c3c9`; M34.4 `fca41195e3dab011cbb2a231b3cc41bdd7baff2d85f5e87ccf9c0bd2e26558af`. These are workspace documents, many untracked; their hashes establish local byte identity, not authorship or historical event provenance.

Sandbox constraints observed: workspace write access is limited to the repository and `/tmp`; no privilege escalation is available or requested. `sudo` was not used. `python3`, `sha256sum`, `stat`, `findmnt`, `lsblk`, `jq`, `ctest`, and `cmake` are present; a standalone `statx` executable is absent, while libc `statx` is available. No administrative path was opened to work around access restrictions.

## 3. Guest identity observed

| Field | Observation | Classification |
|---|---|---|
| `/etc/os-release` | Debian GNU/Linux 13 (trixie), Debian version 13.7 | GUEST_OBSERVED |
| `uname` | Linux `debian`, `6.12.113+deb13-amd64`, x86_64 | GUEST_OBSERVED |
| boot ID | Readable; omitted from this report | GUEST_OBSERVED, session-local and boot-specific |
| machine ID | Readable; only SHA-256 `0ca321499c6e39309457a85790ae5490c1b109b8737dd1000eb21105e5fdfee7` recorded | GUEST_OBSERVED; does not match the M34.1 hardcoded machine ID |
| DMI product name/vendor | `VirtualBox`; `innotek GmbH` | GUEST_OBSERVED; not host attestation |
| DMI product UUID | Unreadable | BLOCKED |
| M34.1 expected hostname/UUID/serial | Collector pins `debian`, product UUID `91ea75e4-…`, serial `VirtualBox-e475ea91-…` and a specific machine ID; current session does not match the pin and the UUID is unreadable | VERIFIED in local collector; mismatch observed for available values |
| VirtualBox host VM UUID, host snapshot UUID/state, snapshot inventory | Not exposed or supplied | UNKNOWN / BLOCKED |

There is no basis to classify anything as `HOST_ATTESTED`. No host-side VirtualBox command, signed host record, VM configuration export, or independently held snapshot inventory was provided. Repeated guest-visible DMI strings are not a substitute. The local workspace is also not established as the intended Debian Alpha M1 guest, despite Debian/VirtualBox traits.

## 4. Mount topology and statx

`/proc/self/mountinfo` was parsed read-only and cross-checked with `findmnt` for the checkout and `/tmp`. No mount was created and no source tree was scanned. The configured collector roots were correlated to their longest matching mount point from mountinfo; this is namespace topology, not an inventory of protected descendants.

| Path(s) | Mount point / ID | Parent ID | Major:minor | Root / filesystem | Propagation and notes |
|---|---:|---:|---|---|---|
| `/usr/local/sbin`; `/etc/sudoers.d`; `/root/athena-alpha-m1-stage`; `/var/lib/athena-alpha-m1`; `/var/lib/monad-m32-1-evidence-20261029T194120Z` | `/`, 455 | 158 | 8:2 | `/`, ext4, `/dev/sda2` | Root mount is a slave (`master:1`) in the observed namespace. All five resolve to this mount in the observed mountinfo. Administrative leaves were not opened or independently statted. |
| `/home/alessandro/Projects/monad` | checkout, 540 | 455 | 8:2 | `/home/alessandro/Projects/monad`, ext4, `/dev/sda2` | Same major/minor as `/`, different mount ID; `findmnt` reports private,slave. A subtree/bind mount is indicated. |
| `/tmp` | `/tmp`, 491 | 480 | 0:40 | `/`, tmpfs | `findmnt` shows private,slave; a read-only enclosing tmpfs mount ID 480 and writable nested mount ID 491 are visible. |

There is no observed overlay for these listed paths. The observed checkout demonstrates why `st_dev` is not a mount-boundary identity: mount ID 540 differs from root ID 455 while major/minor remains 8:2. The collector's descriptor `statx(STATX_MNT_ID)` probe succeeded on ordinary accessible paths (`.`, `/tmp`, `/etc/os-release`), returning the requested mask and nonzero IDs. This verifies syscall availability in this environment only; it does not exercise a real bind-mount boundary in the collector. M34.3/M34.4 synthetic mismatch hooks test classification, not kernel mount behavior.

M34.1 captures the mount ID of each configured root descriptor and requires descendants to match that root's ID. The initial ID is accepted without comparison to a separately approved expected ID; parent components leading to a configured root are not pinned to an expected topology. Root IDs and comparisons are not recorded in the manifest. These are explicit policy/auditability limits. Before any acquisition, an independent reviewer must decide which initial root mounts are acceptable using host/guest evidence and a complete mount view from the identified guest. An unfamiliar, missing, or changing mount identity must stop the procedure.

## 5. Effective M34.1 collector contract

The local collector has fixed roots and no general filesystem search:

| Configured root | Required status / type | Depth and enumeration | Content policy and absence/error behavior |
|---|---|---|---|
| `/usr/local/sbin` | Root is inventoried; runner and `gateway_support.py` required regular files | Global depth 12, 4,000 entries, 8,000 enumerated names | Exact target bytes captured and hashed; missing/type/content/access issues prohibit `COMPLETE`. Other children metadata only unless under another explicit rule. |
| `/etc/sudoers.d` | Root inventoried; `athena-alpha-m1` required regular file | Same global bounds | Exact target bytes captured and hashed; missing/type/content/access issues prohibit `COMPLETE`. |
| `/root/athena-alpha-m1-stage` | Conditional observation; no required leaf | Same global bounds | Exact staged support path captured if present; absence alone is not an issue. Inaccessibility or traversal anomalies still add issues and prevent `COMPLETE`. |
| `/var/lib/athena-alpha-m1` | Root inventoried; exact transaction directory `backups/run-20261021-061921` required directory | Same global bounds | Every regular file under the named transaction subtree can be hashed within 4 MiB/file and 32 MiB total read limits. Only exact `CONTENT_NAMES` are copied; unapproved `.absent`/`.existed` lookalikes are hash-only and flagged. `ext4-fixture` is recorded and pruned. Other files in root are metadata only. |
| `/var/lib/monad-m32-1-evidence-20261029T194120Z` | Root inventoried; `inventory.json` and `SHA256SUMS` required regular files | Same global bounds | Exact two files captured; checksum must contain exactly the expected inventory line and match its bytes. This proves bundle-internal consistency only. |

The six required paths are runner, support, sudoers, named backup transaction directory, M32.1 inventory and checksum. `REQUIRED` contains no expected hashes for the installed admin targets: content is captured, but comparison to trusted release bytes/provenance is a separate review. Per-file read/hash cap is 4 MiB; total source bytes 32 MiB; output inventory cap 8 MiB. Exceeding limits yields an issue/partial result; inability to establish statx mount identity blocks. Missing configured roots generate `ABSENT` and required checks prevent `COMPLETE` where relevant. Parent/path, type, ownership, permission, symlink, special-file, hardlink, mutation, enumeration and checksum errors produce issues; `COMPLETE` requires no issues and all configured roots represented. It describes this collector's contract only.

The initial root mount policy is ambiguous for authorization: the code accepts whatever mount ID each root has at run time, while M34.4 requires an independent topology review. There is no approved mount-ID allowlist or persisted mount decision record. Consequently, successful synthetic tests do not settle whether any actual root mount is intended.

## 6. Evidence chain and provenance

Classifications: **VERIFIED** means directly checked in this workspace/session; **CORROBORATED** means separate evidence supports a claim without authenticating its source; **INFERRED** means a bounded interpretation; **UNKNOWN** means unavailable; **BLOCKED** means a required verification needs unavailable access or authority.

| Link | Current classification | Evidence / limit |
|---|---|---|
| M25 | CORROBORATED as historical report content; underlying event UNKNOWN | M33/M34 local documents say runner digest `5d6ee350…021d5` was attributed to M25. Raw M25 acquisition, signed records and custody were not supplied. |
| M27 | CORROBORATED as local report content; underlying event UNKNOWN | M33/M34 recount runner digest, support absence and incomplete backup observations. Reports are local/untracked; repeated reporting may inherit one source. |
| M32.1 | UNKNOWN for raw acquisition; VERIFIED only that collector source exists locally | M33 says the operator-reported bundle was schema 4, 598 entries, 1176 enumeration entries, zero bytes hashed and `PARTIAL` with support `REQUIRED_ABSENT`. No original bundle, raw stdout/stderr/exit code or custody record was found here. |
| M33 | VERIFIED document bytes; historical VM claims UNKNOWN | Local forensic review correctly limits claims to reported evidence; not an independent raw source. |
| M34 | VERIFIED document bytes; review conclusions depend on supplied/local M33 material | It records the review transition and acquisition plan, not new VM evidence. |
| M34.1 | VERIFIED local collector bytes/hash and current synthetic behavior; actual invocation UNKNOWN | Hash matches expected. No evidence ties these bytes to the historical M32.1 run or an installed collector. |
| M34.2 | VERIFIED audit report/test bytes; tested defect history corroborated by unchanged test failing against fixed collector | The old false-COMPLETE condition is a regression history, not current behavior. |
| M34.3 | VERIFIED local remediation report and collector code; separate origin not authenticated | Code fixes typed required predicates, uses descriptor `statx` mount IDs, and narrows copied marker names. |
| M34.4 | VERIFIED local independent report/test; not a host or VM attestation | Independent synthetic review supports `GO_FOR_OPERATOR_REVIEW`; no real bind mount or protected acquisition was done. |
| M35 | VERIFIED session observations and synthetic suite results | This report records current guest and local code; it does not authenticate past VM events or authorize the next phase. |

Historical administrative figures supplied for correlation: installed runner SHA-256 `5d6ee35070954d97034de7f0323204995fe0ca86ed12fad774a3acb9ada021d5`; current installer runner pin `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def`; current repository support `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514`; support target historically `REQUIRED_ABSENT` according to M32.1. The two runner hashes differ. The reported old digest recurring in M25/M27/M32.1 is not proof of independent measurements, a release generation, or transfer. No repository bytes were compared with installed VM bytes in this session.

The M32.1 collector's `SHA256SUMS` protects the inventory against accidental alteration relative to that checksum file. It is unsigned, can be produced alongside the inventory, and does not authenticate collector bytes, host, guest, operator, time, completeness, or transfer chain. Never promote a digest match into provenance.

## 7. Administrative discrepancy: observations needed, no cause assigned

Current evidence does not distinguish any of the following explanations: a different historical generation, incomplete installation, later modification/removal, artifact relocation/removal, or error/omission in the historical reconstruction. Required evidence to discriminate includes:

| Candidate explanation | Missing observations needed | Severity / pass criterion |
|---|---|---|
| Historical runner generation | Original runner bytes or trusted signed release/source archive matching `5d6ee…`; authenticated manifest/version and link to the installed target/transaction | High; match plus trusted provenance and compatibility record identifies generation. Digest match alone fails. |
| Incomplete installation | Complete no-follow inventory and bytes/metadata for the whole named backup transaction and backup root; all marker states, manifest, transaction record, payloads/sidecars/temp siblings; installer version and execution/audit records | Critical; transaction validates under its actual writer/version and chronology. Missing or contradictory entries remain blocked. |
| Later modification/removal | Earlier/later immutable snapshots or independently preserved audit/configuration-management records tied to the same VM/boot and trusted time; target metadata/bytes | High; temporal chain must demonstrate prior presence and subsequent change. Current absence alone fails. |
| Artifact removed or relocated | Complete target/staging/backup inventory plus package/config-management/audit records and search scope approved in advance | High; documented movement/removal event, or exhaustive bounded evidence that supports a narrower conclusion. |
| Historical reconstruction error | Raw M32.1 bundle, exact command/collector hash, stdout/stderr/exit status, original machine/boot identity and custody transfer; independent checksum/schema revalidation | Critical; raw evidence reproduces or refutes the report under a verified collector identity. |

Do not reconcile, repair, reinstall, rollback, or reinterpret lifecycle state in this phase. Stage absence is not causal evidence: stage is conditional input; installed support is a required output under the current contract. `REQUIRED_ABSENT` means the collector reported a missing required leaf, not that it was historically absent or that a transaction occurred.

## 8. Targeted synthetic checks

No new fixture suite was required. The four specified local suites were run against private temporary fixtures only, with `PYTHONDONTWRITEBYTECODE=1`. The collector digest had matched before these dynamic tests. No collector was invoked on real or administrative paths.

| Suite | Result | Interpretation |
|---|---|---|
| M34.1 `tests/m34_1_targeted_forensic_acquisition_test.py` | PASS 26, FAIL 0, SKIP 0 | Includes required type mismatch, mount-ID unavailable, interruption, mutation, allowlist, bounds and output checksum cases. |
| M34.2 `tests/m34_2_security_audit_test.py` | PASS 17, FAIL 1 expected, SKIP 3 | Historical false-COMPLETE assertion fails because the corrected collector exits 1/PARTIAL. SKIPs: deterministic output I/O fault injection, real same-device bind mount (prohibited), deterministic truncate-during-read hook unavailable. |
| M34.3 `tests/m34_3_security_regression_test.py` | PASS 11, FAIL 0, SKIP 0 | Covers required type checks, synthetic mount mismatch/error, unknown marker, mutation, source preservation and checksum behavior. |
| M34.4 `tests/m34_4_independent_verification_test.py` | PASS 15, FAIL 0, SKIP 0 | Independently inspects manifest results, checksum outputs, descriptor statx availability, failure branches and synthetic mount classification. |
| `statx` ordinary-path probe | PASS | libc call returned `STATX_MNT_ID` mask and nonzero ID for ordinary accessible paths. No actual mount crossing tested. |
| Real mount creation/traversal | NOT RUN | Prohibited; synthetic hook is not kernel-level bind-mount verification. |
| Privileged/VM acquisition, installer, canary, rollback, CTest qualification | NOT RUN | Prohibited/not needed for these local synthetic checks. |
| ShellCheck | NOT RUN | Executable unavailable. |

The single M34.2 FAIL is reported rather than suppressed or relabeled: it asserts exit 0 and `COMPLETE` when a required runner is a directory. M34.3/M34.4 demonstrate the corrected `TYPE_MISMATCH`, `PARTIAL` and nonzero exit. Test outcomes are separate from any Alpha M1 qualification status.

## 9. Risks and readiness findings

1. **Guest identity mismatch — CRITICAL / BLOCKING.** Available guest ID values differ from M34.1's production pin; product UUID is unreadable. Evidence needed: identify the intended VM/snapshot and collect a fresh guest identity under approved procedure. Authorized by: operator/qualification owner. Pass: all approved guest identity fields match a declared identity record; any discrepancy is resolved from independent evidence, not by changing pins ad hoc.
2. **No host attestation — CRITICAL / BLOCKING.** No host VM UUID/snapshot/current-state evidence. Evidence needed: independent host VirtualBox inventory/configuration and snapshot record, bound to the guest session. Authorized by: host/VM custodian and qualification owner. Pass: two-sided correlation establishes the exact VM and snapshot without state transition.
3. **Initial mount policy unresolved — HIGH.** M34.1 accepts each configured root's initial mount and does not persist IDs. Evidence needed: full read-only mount topology from the identified guest and reviewed expected root mapping. Authorized by: security reviewer and operator. Pass: every root mount is approved; no unexplained bind/overlay/tmpfs/separate filesystem; descriptor mount behavior remains available.
4. **Evidence provenance/custody absent — CRITICAL.** Raw M32.1 bundle, M34.1 historic invocation identity, output logs and authenticated transfer chain are not available. Evidence needed: preserved original bundle, exact collector digest/signature or other independent approval binding, raw process status/output and custody ledger. Authorized by: evidence custodian and independent reviewer. Pass: schema, checksums, scope, collector identity, and custody all verify.
5. **Administrative state unreconciled — CRITICAL.** Runner mismatch and missing support are historical reports only; transaction inventory is incomplete. Evidence needed: full read-only acquisition of approved target/backup/log scope and trusted historical artifacts. Authorized by: qualification owner, evidence custodian, security/operations reviewers. Pass: each artifact and transaction is explained with primary evidence, or state is explicitly deemed indeterminate and a separately approved disposition is made.
6. **Collector provenance/promotion — HIGH.** Local digest matches the requested expected digest but local qualification scripts are untracked and no release signature/attestation is shown. Evidence needed: approved promotion record tying reviewed bytes to the exact executable delivered and invoked. Authorized by: designated release/pin reviewer. Pass: independent trust anchor and exact-byte execution binding are documented.
7. **M34.2 historical FAIL and three SKIPs — MEDIUM.** They are understood, not concealed. Evidence needed: keep the historical failure mapped to remediation; any closure requires an updated test contract reviewed separately and real mount/I/O coverage only in an authorized suitable environment. Authorized by: independent verification/security reviewers. Pass: no test asserts unsafe behavior as a product requirement; remaining environment gaps are explicitly accepted or closed.

No confirmed new defect in the tested M34.3 paths was found. The most material residual risk for authorization is environmental: the exact guest/root mount identities are not established and M34.4 explicitly leaves initial root mounts to independent review. Readiness to review an authorization is distinct from authorization and from qualification.

## 10. Future operator checklist (non-executable plan)

This checklist is a review aid only. It contains no acquisition command and grants no authorization.

1. **A — Explicit operator authorization:** obtain a written, time-bounded decision naming operator, reviewer, VM and snapshot, exact read scope, data handling, output parent, custodian and stop authority. Stop if scope or authorization is ambiguous.
2. **B — Guest identity:** on the already-running guest, record OS release, kernel/architecture, boot ID, machine ID (protected or redacted), DMI fields, and approved identity comparison. Do not change guest configuration or pins to force a match.
3. **C — Independent host attestation:** obtain host-side VirtualBox VM UUID, snapshot UUID/current snapshot and inventory from the custodian; correlate with guest observations. If the session is not the expected VM/snapshot or host evidence is unavailable, stop.
4. **D — Mount topology:** preserve read-only `mountinfo` and relevant path metadata, review mount point/root/type/ID/parent/device/propagation for every configured root and ancestor. Resolve the collector's accepted-initial-mount policy before proceeding. Stop on unexplained topology, crossing, unavailable IDs, or changes.
5. **E — Collector hashes:** verify the approved collector digest from an independent trusted source, then bind the executed bytes to those reviewed bytes (immutable controlled copy or equivalent reviewed mechanism). A local checksum alone is insufficient.
6. **F — Allowlist:** independently compare exact roots, required paths/types, content names, depth/entry/enumeration/byte limits and hash rules in the reviewed collector with the approved scope. Do not widen limits or names during acquisition.
7. **G — Administrative state:** confirm the observed state and prior evidence are preserved; no install, rollback, canary, mount, systemd or reconciliation action is in scope. Treat access denial as BLOCKED; do not escalate or retry with privilege outside separately approved authority.
8. **H — Exclusive output:** name one new private absolute output directory outside repository, source roots, backup/evidence roots, fixtures and administrative paths. Verify it does not exist and parent ownership/permissions are acceptable. Never reuse or overwrite a destination.
9. **I — Read-only sources:** collect only approved fixed paths using the reviewed bytes; do not follow symlinks or inspect unapproved content. Preserve errors and raw output; do not modify, normalize, repair or clean sources.
10. **J — Exit status:** preserve exact exit code, stdout/stderr, start/end time and operator identity. Any nonzero exit, signal, timeout, interruption or output error is not success; preserve partial output and stop.
11. **K — SHA256SUMS:** independently verify every listed output digest and confirm the checksum file names only expected output objects. Record verification tool/version/result separately; do not treat the checksum as provenance.
12. **L — Manifest schema:** validate schema, required fields, collector ID, identity, root coverage, issue list, limits/counts and captured-file references. Confirm every required file row has expected type, captured bytes and digest; validate M32.1 inventory/checksum relation. Reject missing, duplicate, malformed or inconsistent records.
13. **M — Classification:** `COMPLETE` only if all approved contract predicates pass with no unresolved issue; `PARTIAL` for observed gaps/anomalies/limits; `BLOCKED` for identity, mount-ID, authorization, integrity or execution guarantees not established. Collector `COMPLETE` is not acquisition authorization or Alpha qualification.
14. **N — Anomalies:** preserve output and source state; isolate working copies for analysis; notify named reviewer through the approved process. Never repair markers, rerun with broader scope, retry under privilege, or make a causal claim from absence alone.
15. **O — Evidence preservation:** preserve original output, raw logs and transfer metadata with independent hashes, read-only custody, access log and separate analysis copies. Retain the approved D6 residual fixture untouched.
16. **P — Stop criteria:** identity mismatch/uncertainty; host attestation absent; unexpected or unapproved initial mount; mount-ID unavailable/change; symlink/type/owner/permission/hardlink anomaly; access denial; source mutation; checksum/schema mismatch; resource limit; nonzero exit; timeout/interruption; output collision/write/fsync failure; custody gap; or any need to modify state or expand scope. Stop and preserve; do not clean up.
17. **Q — Future reconciliation gate:** a future reconciliation requires a separate written human decision after primary evidence review explains or explicitly dispositions the runner/support/transaction discrepancy, validates candidate provenance and compatibility, protects D6, verifies administrative policy and backup/recovery plan, and defines exact root-owned hash-pinned artifact/argument allowlist. This checklist itself authorizes no state change.

## 11. Adversarial review and human decisions

The plan has no executable acquisition command, but an operator could still misread collector `COMPLETE` as host identity, source authenticity or qualification. The checklist explicitly rejects that interpretation. SHA256SUMS is internally useful but self-contained; it needs an external trust anchor. Guest values may be cloned or stale and boot-specific values vary; host VM/snapshot records must be independent. Mount IDs are namespace observations and can vary after remount/reboot; their expected values need a declared policy rather than a guessed literal. A live walk is not an atomic snapshot. Filesystem absence cannot establish historical nonexistence. The output destination is a write and must be explicitly within the authorized evidence-handling procedure even though sources are read-only.

Human decisions necessarily remain for: which VM/snapshot is the target; whether current root mounts are expected; what log/time scope and sensitive evidence may be acquired; whether a primary-evidence gap is acceptable; whether any later reconciliation is justified; and who may authorize any privileged read or state-changing action. No automated test or hash can answer these governance/evidence questions.

## 12. Criteria to change the decision

| Residual block | Required evidence | Severity | Approver | Pass criterion |
|---|---|---|---|---|
| Intended guest not identified | Guest identity record and host VM/snapshot attestation | Critical | VM custodian + qualification owner | Independent two-sided identity correlation agrees with approved target. |
| Root mount baseline not approved | Full mountinfo/topology and expected mount policy | High | Security reviewer + operator | Every configured root's initial mount and ancestors are understood and accepted. |
| M34.1 provenance not bound to execution | Independent promotion/signature record and exact-byte invocation binding | High | Release/pin reviewer | Approved collector bytes match the executed bytes and scope. |
| Historical evidence unavailable | Raw M32.1 output, process logs/status, custody ledger | Critical | Evidence custodian + independent reviewer | Integrity, schema, collector identity and transfer chain independently pass. |
| Admin discrepancy unresolved | Complete primary evidence for targets, transaction, backup and relevant history | Critical | Qualification owner + security/operations | Cause is supported by primary evidence or explicitly dispositioned; no unsupported inference remains. |
| Historical test debt | Review of intentional M34.2 assertion and explicit disposition of 3 skipped fault/mount cases | Medium | Independent verification/security | Remediation is accepted; environment-only gaps are bounded and recorded. |

Passing these conditions only permits a new authorization review. A separately authorized acquisition still requires its own explicit operator approval. Alpha M1 qualification additionally requires the approved Debian VM campaign to finish every required privileged case and evidence review.

## 13. Final verdict

**`BLOCKED_INSUFFICIENT_EVIDENCE`.** Local collector integrity and synthetic regression behavior are satisfactory for review, but the current guest is not the collector's pinned production identity, its DMI product UUID cannot be read, no independent VirtualBox host/snapshot attestation exists, and primary M32.1/admin evidence is absent from this workspace. The initial mount acceptance policy is not externally pinned or recorded. These gaps prevent an operator-readiness finding and make privileged acquisition inappropriate from this environment.

This verdict does not assign a cause to the administrative discrepancy, does not authorize acquisition or reconciliation, and does not change **Alpha M1: NO-GO / NOT QUALIFIED**.
