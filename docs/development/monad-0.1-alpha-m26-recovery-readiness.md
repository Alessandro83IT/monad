# Monad 0.1 Alpha M26 — Forensic reconciliation and recovery readiness

**Decision: NO-GO for administrative recovery or Alpha M1 qualification.** This report records a source and historical-evidence review. M25 VM facts below are operator-supplied observations; this session did not access the VM or mutate protected state. Recovery is documented for later review only and was not executed.

## 1. Executive summary

The current installer publishes three files: runner `/usr/local/sbin/athena-alpha-m1-runner`, support `/usr/local/sbin/gateway_support.py`, and sudoers `/etc/sudoers.d/athena-alpha-m1` (`qualification/install-athena-alpha-m1-admin.sh:8-23`). M25 reports the runner and sudoers present, matching the correct destinations; support destination was not reported. The observed empty `install-started` conflicts with current code, which writes `transaction=<id>\n` (`:688-693,824`). `runner.absent` and `sudoers.absent` record original pre-install absence, not current state. The transaction therefore remains indeterminate.

Current source hashes equal installer literals, but M25's historical runner and canary hashes differ. The receipt's boot ID is old. No exact archived artifact matching the runner hash was found in the accessible repository evidence; origin and authentication remain unknown. M25 does not resolve inventory, pin provenance, D6 custody, privileged campaign, or independent review blockers.

## 2. Git state and method

**VERIFIED BY CODEX:** branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`, tracking `origin/main`. The worktree was already substantially dirty, including tracked C++/test edits and untracked reports, build directories, qualification artifacts, and `AGENTS.md`. These were treated as user work. Only this new report was created; no existing source, pin, inventory, receipt, or evidence was changed.

The review used local source, reports, repository history availability, and hashes. No VM connection, external network, privileged command, operational script, test, or build was run. Evidence labels in this report mean: **VERIFIED BY CODEX** (local bytes/source inspected), **OPERATOR OBSERVATION** (M25 input), **HISTORICAL DOCUMENT CLAIM** (prior report), **INFERENCE** (reasoned possibility), **UNKNOWN** (not established).

## 3. M25 evidence reconciliation

All following VM details are **OPERATOR OBSERVATION**, not independently acquired: VM UUID `e475ea91-72e9-4157-9ee6-583e9ae0462f`, machine ID `9bdbfd8678b5467cb85e4d8aa5e01fea`, boot `58fa9502-d0d3-4b02-b57b-82666e5a2b2b`, Debian 13, kernel `6.12.113+deb13-amd64`, systemd running, no loaded/failed matching units. The reported runner is root:root `0755`, 15990 bytes, SHA-256 `5d6ee350…021d5`; installed sudoers is root:root `0440`, 1848 bytes, SHA-256 `263c1197…36c18`. Support presence, all path ACLs, stage files, transaction manifest and complete backup-root inventory remain unknown.

The transaction directory is reported root:root `0700`; `install-started`, `runner.absent`, and `sudoers.absent` are regular root:root `0600`, zero bytes; completion markers are reportedly absent. Current code requires a transaction payload for `install-started`; the empty report is a direct contract mismatch. Absence markers are expected to be empty but describe captured original presence only (`:786-808`; transaction validation around `:389-470`). The remaining marker set, payloads, sidecars, transaction JSON, manifests, lock, temporary files, and other transactions are unknown.

The receipt report gives SHA-256 `863872f2…181026`, schema 1, passed true, runner `5d6ee350…021d5`, canary `5919ef00…02a148`, boot `3fec68c2-2d94-45ea-b574-00249796f987`, systemd `257.13-1~deb13u1`. It is from another boot and cannot meet current-boot validation. The receipt's reported runner/canary digests also differ from current source pins. Its raw bytes and custody are not independently verified.

M25 says `/var/tmp/athena-d67-ext4` is empty and the historical fixture `/var/lib/athena-alpha-m1/ext4-fixture/athena-d6-8yRDjc` contains generations, projection, store and a lock. Preserve both accounts as supplied evidence. The installer requires a separate canonical fixture to be empty and ext4 (`:216-228`); the reported nested historic fixture means this prerequisite is not established. Never reuse or delete that fixture.

## 4. Installer contract

### Paths, pins, metadata

| Artifact | Stage input | Published target | Current expected metadata | Local pinned SHA-256 |
|---|---|---|---|---|
| Runner | `/root/athena-alpha-m1-stage/athena-alpha-m1-runner.py` | `/usr/local/sbin/athena-alpha-m1-runner` | root:root `0755` | `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` |
| Support | `/root/athena-alpha-m1-stage/gateway_support.py` | `/usr/local/sbin/gateway_support.py` | root:root `0644` | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` |
| Sudoers | `/root/athena-alpha-m1-stage/athena-alpha-m1-sudoers` | `/etc/sudoers.d/athena-alpha-m1` | root:root `0440` | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` |
| Canary | `/root/athena-alpha-m1-stage/athena-alpha-m1-systemd-canary.py` | **not installed**; staged prerequisite only | protected stage file | `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` |

The stage and destinations are at `qualification/install-athena-alpha-m1-admin.sh:8-23`; expected installed metadata and hash checks are `:741-754`. Temporary publication files are created adjacent to each target with `.athena-alpha-m1-runner.*`, `.athena-alpha-m1-support.*`, `.athena-alpha-m1.*` prefixes (`:811-813`). Other relevant paths include `/var/lib/athena-alpha-m1/admin-install.lock`, `/var/lib/athena-alpha-m1/backups`, `/var/lib/athena-alpha-m1/systemd-canary.json`, `/var/lib/athena-alpha-m1/ext4-fixture`, `/var/log/athena-alpha-m1`, and protected build `/opt/athena-qualification/build` (`:16-19,713-725`). The sudoers draft allows 18 fixed runner invocations, not a shell or wildcard (`qualification/athena-alpha-m1-sudoers:1-22`).

### Transaction semantics and discrepancy

Installer preflight validates path components/ACLs, source pins, receipt, protected build pins and fixture; obtains an exclusive lock; and refuses any backup-root transaction lacking `install-complete` or `rollback-complete` (`:123-203,230-273,713-739`). It captures originals and metadata sidecars or empty `.absent` markers, then persists transaction and backup manifests and `prepared` before publication (`:786-808`; validator `:389-470`). Candidate files are staged in target parents. `install-started` is fsynced with `transaction=<backup directory basename>` before the first target rename (`:688-693,811-826`). Publication is three independent renames in runner, support, sudoers order, followed by verification and `install-complete` (`:827-842`). There is no atomic three-file commit.

On ordinary post-boundary failure, an EXIT trap attempts an `install-partial` payload and preserves the transaction; SIGKILL/power loss may bypass it (`:672-685`). Rollback is a separate explicit operation requiring transaction ID and approval token bound to transaction and manifest hashes; it restores/removes targets individually, validates sudoers, records statuses, and writes `rollback-complete` only at the end (`:495-566,630-668,849-857`). A partial rollback can leave mixed state; never infer success from a missing marker.

M25 discrepancies against this checkout: (1) empty `install-started` versus required payload; (2) runner and sudoers present while `.absent` markers describe prior absence, which is not contradictory by itself; (3) no completion marker means neither successful transaction completion nor rollback is evidenced; (4) runner hash differs from current pin; (5) support destination unreported; (6) absent historical receipt boot and artifact hashes cannot satisfy current installer preflight; (7) nonempty historical D6 child conflicts with treating the fixture as ready. The checkout is not established as the installer revision used on 2026-10-21.

## 5. Historical artifact matching

**VERIFIED BY CODEX:** local `sha256sum` results:

| Local file | SHA-256 | Match status |
|---|---|---|
| `qualification/athena-alpha-m1-runner.py` | `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` | Exact current installer pin |
| `qualification/gateway_support.py` | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` | Exact current installer pin |
| `qualification/athena-alpha-m1-systemd-canary.py` | `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` | Exact current installer pin |
| `qualification/athena-alpha-m1-sudoers` | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` | Exact current installer pin; also M25-reported installed byte digest only |
| `qualification/install-athena-alpha-m1-admin.sh` | `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7` | Historical installer identity is not established by this hash |

The receipt runner hash `5d6ee350…021d5` and canary hash `5919ef00…02a148` are **OPERATOR OBSERVATION** and differ from current source. The M19 and M24 reports make the same mismatch claim; M15/M16 documentation and source-closure records describe prior candidate review but do not authenticate the bytes installed on the VM. No exact repository object/archive match to `5d6ee…` is documented in the accessible M14–M16/M17/M19/M24 evidence examined. Exact historical origin: **UNKNOWN**. A matching hash establishes content equality only; inferred association to a date/revision is not authenticated provenance. Installed sudoers matching local bytes likewise does not establish who installed it or under what authorization.

Seven protected build pin discrepancies and incomplete official inventory remain as reported by M16 (`docs/development/monad-0.1-alpha-m16-gate-review-20261028.md:38,82,123`). No pin was promoted or altered.

## 6. Transaction reconstruction

The observed state cannot select one hypothesis:

| Scenario | Consistency with observations | What would distinguish it |
|---|---|---|
| A. Interrupted installation | Possible if an installer began publication; marker bytes then conflict with current writer. | Full transaction manifest/markers, target hashes, temp files, logs/journal, exact installer candidate and evidence of process interruption. |
| B. Partial publication | Possible: separate renames permit runner/support/sudoers mixtures. Runner/sudoers presence alone does not reveal which generation. | Hash/metadata of all three targets, candidate hashes in transaction manifest, backup originals and per-target statuses. |
| C. All targets published, bookkeeping incomplete | Possible; interruption after final rename and before completion can do this. | All three target hashes, transaction record, logs and marker bytes; cannot infer from two reported paths. |
| D. Later manual modification | Possible: a post-transaction actor could publish/replace targets or truncate marker. | Audit/journal/shell/config-management records, inode/ctime evidence, trustworthy acquisition and custody records. Metadata alone may not identify actor. |
| E. Historical installer revision mismatch | Possible and materially plausible because marker format differs from current checkout. | Archived installer hash/source, release bundle, staged copies, transaction schema and matching historical code. Date/name alone is insufficient. |
| F. Overlapping operations | Possible if concurrency protections differed, were bypassed, or state was manually changed; current lock is intended to serialize. | Lock metadata/inode, complete transaction-root inventory, process/journal records, multiple transaction IDs and timestamps. |

Minimum next observations: one identity-bound read-only acquisition of full `/var/lib/athena-alpha-m1/backups` including raw marker bytes and every hidden entry; transaction JSON, manifests, payloads/sidecars/statuses; all three exact destinations and stage files with no-follow type/owner/mode/ACL/link/hash; admin lock and log directory; receipt and archives; installer copies/hashes and available journal/audit records; systemd units/processes/cgroups; and D6/fixture mount metadata. Tie capture to VM UUID, machine ID, boot ID and UTC; preserve raw output and hashes. Do not run install or rollback before independent transaction reconstruction.

## 7. Canary and D6 readiness

Canary prerequisites in current installer: safe root-owned stage and state paths; canary and runner source pins; a receipt at `/var/lib/athena-alpha-m1/systemd-canary.json`, regular root-owned mode `0600`; schema 1, `passed: true`, matching canary/runner hashes, integer `validated_ns` no more than 300 seconds in the future and no older than 24 hours, receipt boot ID equal current `/proc/.../boot_id`, and exact current `systemctl --version` first line (`:230-272`). Runtime canary requirements and receipt publication/termination rules are documented in M19 and M24; the canary must not be run in this session.

M25 receipt fails current boot binding and current hash binding by supplied values. Systemd running and no loaded/failed unit reports do not prove cgroup cleanup, receipt validity, or readiness. D6 original custody and canonical empty ext4 fixture are independent requirements. Preserve `/var/tmp/athena-d67-ext4`, the reported historical nested fixture and all related logs/reports/backups. Never mount, traverse payload content, delete, rename, or reuse the historical fixture. Current installer refuses a nonempty fixture (`:216-228`).

## 8. Recovery decision tree (proposal only)

Every command below is documentation only. Each branch requires a separately approved, identity-bound administrative change request after read-only evidence acquisition and independent review. Commands are illustrative templates; do not substitute paths or run until exact artifacts, VM, boot, and command hash are approved.

### Branch 1 — Transaction fully proven complete

**Entry:** canonical transaction and manifest validate; all three targets match that transaction's candidates and expected metadata; sudoers passes syntax and full policy review; logs and markers explain the empty `install-started`; no unexplained temporary entries; source provenance and candidate authorization are established. **Action:** no recovery mutation. Record current state and retain all artifacts. Expected result: independently reviewed `COMPLETE / NO MUTATION`. If any mismatch, return to Branch 4. No rollback is justified merely to normalize markers.

### Branch 2 — Original state proven intact; no publication occurred

**Entry:** evidence proves all targets equal captured originals or original absence, there was no `install-started` boundary in the installer version actually used, no runtime effects, and backup manifest is complete. Proposed operator command after a separate authorization checkpoint: `sudo /usr/bin/stat ...` and `sudo /usr/bin/sha256sum ...` are read-only examples; acquire the approved full inventory as a signed evidence bundle. Do not rerun install under current uncertainty. A later installation would require a new reviewed transaction, fresh canary and fixture/build gates. Expected output is a complete raw inventory and independently signed review. Failure: stop and preserve; no cleanup.

### Branch 3 — Mixed or changed publication, recoverable backup

**Entry:** forensic review proves exact transaction ID, authenticated original backup payloads/sidecars, candidate and current target generations, and a human selects restore. Preconditions: separate rollback authorization names VM UUID, current boot, transaction ID, backup-manifest SHA-256, exact installer artifact SHA-256, and approval-file SHA-256; no concurrent admin process; all evidence copied immutably and independently reviewed. The current interface requires an approval token and exact transaction identity (`:756,849-857`). Documentation-only command shape:

```sh
sudo /path/to/reviewed/install-athena-alpha-m1-admin.sh rollback \
  /var/lib/athena-alpha-m1/backups/TRANSACTION_ID \
  --transaction-id TRANSACTION_ID \
  --approval-file /path/to/approved-rollback-token.json \
  --approval-sha256 APPROVED_TOKEN_SHA256
sudo /usr/sbin/visudo -c
```

Expected output is installer-reported rollback completion and successful `visudo -c`, followed by a new read-only hash/metadata acquisition of all targets and manifest/markers. Atomicity is only per file. On any error, `PARTIAL ROLLBACK`, changed unexpected target, or missing completion marker: stop, preserve mixed state and all evidence; do not retry automatically. A further recovery requires new independent review and authorization. This template is not authorization.

### Branch 4 — Indeterminate or backup/provenance invalid

**Entry:** missing/malformed transaction data, empty unexpected marker, unverified original payload, unknown installer version, contradictory target evidence, missing support information, unexplained actors, or D6/receipt/build gate unresolved. **Action:** NO-GO; no install, rollback, marker repair, manual replacement, or cleanup. Preserve full evidence. Obtain only the minimum read-only acquisition listed in §6, then independent forensic and security review. Expected result is a bound evidence bundle and explicit hypothesis table. Any access denial or state change keeps branch open.

### Invariants for all later authorized branches

Never overwrite an existing artifact or evidence path; use a new exclusive evidence directory and hash outputs. Retain original transaction, receipt and archived receipts, D6 fixture, logs, staged candidates and backups. No blind installer rerun; no automatic rollback; no change to installed sudoers without separately reviewed authority. Verify VM/snapshot identity and boot before each state-changing step. Recovery completion is not Alpha M1 qualification.

## 9. Qualification blockers

| Blocker | Status | M25 effect |
|---|---|---|
| Official inventory gap | OPEN | None; M25 is administrative evidence, not the authoritative inventory. |
| Seven protected binary pin discrepancies | OPEN | None; no artifact acquisition/provenance or pin promotion occurred. |
| Historical provenance | OPEN | Runner hash remains unidentified; sudoers byte match does not establish provenance. |
| D6 custody and fixture isolation | OPEN | M25 supplies observations but no custody-bound inventory; reported fixture is nonempty and must be preserved. |
| Administrative transaction uncertainty | OPEN | Empty marker conflicts with source contract; full backup tree and support target absent from supplied observation. |
| Canary freshness | OPEN | Receipt boot and artifact hashes are stale for current contract. |
| Privileged campaign | OPEN | Not run or authorized here; no PASS claimed. |
| Independent final review | OPEN | M26 report is not independent authorization/review. |

M25 partially improves the descriptive record of runner and sudoers state, but no gate blocker is resolved. The older M14–M16, M17, M19 and M24 conclusions remain applicable unless superseded by authenticated new evidence.

## 10. Verification and checks

**PASS:** read-only Git branch/HEAD/status inspection; source/report inspection; local SHA-256 calculation for runner, support, canary, sudoers and installer. Hash results are listed in §5.

**NOT RUN:** CTest, Python tests, builds, canary, runner, collector, operational installer, privileged driver, `visudo`, VM or protected filesystem checks. Tests were not requested as a separate implementation task and operational checks are expressly prohibited. No FAIL/SKIP/TIMEOUT result is claimed.

## 11. Verdict and next authorized action

**NO-GO** for recovery, installation, rollback, canary, privileged campaign, or Alpha M1 qualification. The M25 state admits multiple transaction histories and is not safely actionable from current evidence.

**Minimal next action:** authorize a single bounded, read-only, identity-bound acquisition on Athena-Alpha-M1 of the complete administrative transaction tree and all three actual destinations (including support), staged candidates, receipt/archives, installer identity, runtime units/cgroups, logs, and D6/fixture metadata. Preserve raw output under a new exclusive evidence path, tie it to VM UUID/machine ID/current boot/UTC, and obtain independent review before any state-changing recovery proposal. No recovery command in this report is authorized by this recommendation.

## Source references

- Installer path/pin contract: `qualification/install-athena-alpha-m1-admin.sh:8-23`.
- Fixture and receipt checks: `qualification/install-athena-alpha-m1-admin.sh:216-272`.
- Destination checks: `qualification/install-athena-alpha-m1-admin.sh:741-754`.
- Marker writer, interruption trap and publication order: `qualification/install-athena-alpha-m1-admin.sh:672-693,786-842`.
- Incomplete-transaction guard and rollback interface: `qualification/install-athena-alpha-m1-admin.sh:728-739,756-857`.
- Sudoers allowlist: `qualification/athena-alpha-m1-sudoers:1-22`.
- Historical reports: `docs/development/monad-0.1-alpha-m17-d6-reconciliation.md`, `monad-0.1-alpha-m19-admin-reconciliation.md`, `monad-0.1-alpha-m24-macroqualification-review.md`, `monad-0.1-alpha-m16-gate-review-20261028.md`.
