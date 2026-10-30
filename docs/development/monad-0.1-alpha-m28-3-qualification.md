# Monad 0.1 Alpha M28.3 forensic acquisition qualification

## Executive summary

M28.3 is an additive collector revision intended to correct M28.2's missing filesystem-boundary checks and its treatment of every missing target as optional. The collector now compares both device and Linux mount identity against the opened acquisition root, records `FILESYSTEM_BOUNDARY`, and stops conservatively with a partial result when mount identity cannot be read. Installer-derived required roots report `REQUIRED_ABSENT`; only the transient `/run/athena-alpha-m1` root is optional. The historical `/var/tmp/athena-d67-ext4` root remains required for this campaign, and current absence is explicitly not treated as proof of historical absence.

The suite runs only against a synthetic fixture. It does not validate production VM identity, privileged path permissions, real mount crossings, or historical evidence. One socket test is skipped because the sandbox denies AF_UNIX bind. Output-write fault injection was tested; output-fsync fault injection is implemented but not separately exercised. Descriptor-close failures and the root-only synthetic-mode refusal were not dynamically tested. M28.3 is **NO-GO for privileged acquisition** pending explicit independent code review and authorization. Alpha M1 release qualification is not established.

## Exact files created

- `qualification/m28-3-readonly-acquisition.sh`
- `tests/m28_3_readonly_acquisition_test.py`
- `docs/development/monad-0.1-alpha-m28-3-qualification.md`

These are additive files; M28, M28.1, and M28.2 artifacts were preserved.

## Defects addressed and security invariants

- Restored per-acquisition-root `st_dev` and Linux mount-ID baselines. Every opened descendant is compared against both; a mismatch is recorded as `FILESYSTEM_BOUNDARY`, makes completion `PARTIAL`, and prevents descent.
- Mount IDs are obtained from `/proc/self/fdinfo/<fd>` for the already-open descriptor. An unavailable ID is recorded as `MOUNT_ID_UNAVAILABLE`; the collector stops that root rather than treating device equality as sufficient.
- Added source-derived `OPTIONAL_ABSENT` versus `REQUIRED_ABSENT`. `MISSING_INTERMEDIATE_DIRECTORY`, `DISAPPEARED_DURING_TRAVERSAL`, and access/open errors remain separate outcomes.
- Kept descriptor-relative traversal, `O_NOFOLLOW`, special-file non-opening, metadata-only collection by default, exact empty-lock hash authorization, fixed production identity checks, fixed allowlist, safe output-parent checks, exclusive output creation, 0700 directory and 0600 files, checksum manifest, fsync, and preservation of partially created output.
- Output writes now reject a zero-progress write. Output `OSError` returns status 2 and leaves created evidence in place; the broad output `Exception` catch was narrowed so programming errors are not deliberately converted into ordinary output failures.

## Source-derived path inventory and semantics

The fixed inventory is inherited from the prior collector and cross-checked against `qualification/install-athena-alpha-m1-admin.sh`, the M28 plans/reviews, and the historical D6 fixture contract. Installer paths with successful-install or post-install roles are required. The installer creates the log/state roots and lock, requires authenticated build inputs and installed runner/support/sudoers material, and uses stage, backup, receipt, and fixture paths. `/run` is volatile runtime state. `/var/tmp/athena-d67-ext4` is a historical campaign location, not an installer-created path; a present-day missing result does not establish historical absence.

| Allowlisted root | Expected semantics | Basis |
|---|---|---|
| `/usr/local/sbin/athena-alpha-m1-runner` | Required | Installer installation/postcondition |
| `/usr/local/sbin/gateway_support.py` | Required | Installer installation/postcondition |
| `/etc/sudoers.d/athena-alpha-m1` | Required | Installer installation/postcondition |
| `/var/lib/athena-alpha-m1/admin-install.lock` | Required | Installer lock acquisition; exact empty file is sole production hash exception |
| `/var/lib/athena-alpha-m1` | Required | Installer state root |
| `/var/lib/athena-alpha-m1/backups` | Required | Installer transaction backup root |
| `/root/athena-alpha-m1-stage` | Required | Installer stage/source contract |
| `/run/athena-alpha-m1` | Optional | Transient runtime state; absence is valid only with accessible parent |
| `/var/log/athena-alpha-m1` | Required | Installer log root |
| `/var/lib/athena-alpha-m1/systemd-canary.json` | Required | Installer canary receipt contract |
| `/var/lib/athena-alpha-m1/ext4-fixture` | Required | Installer fixture contract |
| `/var/tmp/athena-d67-ext4` | Required for this campaign; historical absence unproven | M28/D6 historical fixture reference, not current installer creation |
| `/opt/athena-qualification/build` | Required | Installer authenticated build precondition |

Absence of a required object is partial. Absence of an optional object is not an issue only when its direct parent is accessible and present. Failure to open an ancestor is `MISSING_INTERMEDIATE_DIRECTORY` or `PATH_ACCESS_ERROR`; a target that disappears after traversal begins is `DISAPPEARED_DURING_TRAVERSAL`.

## Mount-boundary implementation and limitations

For each recursively acquired root, the first opened descriptor establishes `(st_dev, mnt_id)`. Every recursively opened child is compared to that pair. Both `st_dev` and mount ID must agree. Linux `mnt_id` is read from the kernel's fdinfo for the descriptor; this avoids relying on `st_dev` alone. The test suite injects a synthetic boundary status at a known fixture descendant and injects unavailable mount identity. No mount or privileged operation was used.

The mount-ID reader uses a normal read-only open of procfs fdinfo. A kernel/procfs implementation that omits `mnt_id`, denies fdinfo access, or changes behavior causes a partial result. The synthetic tests do not validate kernel mount namespaces, bind mounts, overlay behavior, or a genuine device transition.

## Error handling and resource limits

Path stat, open, traversal/enumeration, and hash `OSError`s are represented locally as issue records and produce partial evidence. Output creation/write/fsync errors return nonzero and preserve any destination or files already created. Hashing is still restricted to the exact source-authorized empty lock file in production; synthetic tests can add a test-only explicit fixture hash path. Unknown files remain metadata-only, including large files. Symlinks and special files are never opened for content.

The inherited strict limits are depth 32, 10,000 entries, 256 MiB total authorized hash bytes, 64 MiB per authorized file, and 8 MiB inventory output, with hard caps of depth 128, 100,000 entries, 2 GiB total hash bytes, 512 MiB per file, and 32 MiB output. Directory snapshots stop at the remaining entry budget. A reached limit makes the report partial; oversized full inventories are replaced with a compact partial report when that fits.

Remaining containment gaps are explicit: descriptor-close failures do not have a dedicated local issue record; metadata verification `fstat` failure paths are not fault-injected; output fsync failure has a test hook but no named executed test; and only selected filesystem errors have deterministic fault injection. Programming exceptions in traversal are not blanket-caught, so an unexpected programming error may terminate without a complete report. These gaps require review before privileged use.

## Test-to-requirement matrix

Command for every executable case below: `python3 tests/m28_3_readonly_acquisition_test.py`. The suite is one unittest invocation; individual names are the reproducible case selectors with `python3 tests/m28_3_readonly_acquisition_test.py AcquisitionTests.<name>`.

| Requirement / named test | Expected | Actual observed |
|---|---|---|
| Successful synthetic acquisition and checksum — `test_valid_acquisition_and_checksum` | Exit 0, COMPLETE, matching manifest | PASS |
| Missing optional root — `test_optional_absent_and_required_absent_differ` | `OPTIONAL_ABSENT`, COMPLETE | PASS |
| Missing required root — `test_required_absent_is_partial` | `REQUIRED_ABSENT`, PARTIAL | PASS |
| Missing intermediate — `test_missing_intermediate_directory_is_not_absence` | `MISSING_INTERMEDIATE_DIRECTORY` | PASS |
| Concurrent disappearance — `test_concurrent_disappearance_and_identity_mismatch` | `DISAPPEARED_DURING_TRAVERSAL` | PASS |
| Symlinked root/descendant — `test_symlink_target_and_descendant_are_rejected` | Never followed; partial | PASS |
| Symlinked ancestor — `test_symlinked_ancestor_is_access_error` | Ancestor not followed; error result | PASS |
| FIFO — `test_fifo_is_not_opened` | Special file metadata only | PASS |
| Unix socket — `test_unix_socket_is_not_opened` | Special file metadata only | SKIP (sandbox AF_UNIX bind returned EPERM) |
| Device boundary — `test_device_and_mount_boundaries_are_partial` | `FILESYSTEM_BOUNDARY`, PARTIAL | PASS (injected; not a real device transition) |
| Mount boundary — `test_device_and_mount_boundaries_are_partial` | `FILESYSTEM_BOUNDARY`, PARTIAL | PASS (injected; no mount performed) |
| Mount identity unavailable — `test_mount_identity_unavailable_stops_conservatively` | `MOUNT_ID_UNAVAILABLE`, conservative partial | PASS (injected) |
| Directory mutation — `test_file_and_directory_mutations_are_recorded` | Mutation recorded, PARTIAL | PASS |
| File mutation — `test_file_and_directory_mutations_are_recorded` | Mutation recorded, PARTIAL | PASS |
| Hash read failure — `test_hash_read_error_is_recorded_partial` | `HASH_READ_ERROR`, PARTIAL | PASS (injected) |
| Large metadata-only file — `test_metadata_only_large_file_and_unauthorized_content_not_read` | No digest/content read; metadata result | PASS |
| Unauthorized content not read — `test_metadata_only_large_file_and_unauthorized_content_not_read` | No digest; zero authorized bytes | PASS |
| Entry/depth/output limits — `test_entry_depth_hash_and_output_limits_return_partial` | Bounded partial report | PASS |
| Existing output directory — `test_existing_output_destination_rejected` | Nonzero; existing directory untouched | PASS |
| Unsafe output parent — `test_unsafe_output_parent_rejected` | Reject | PASS |
| Output checksum — `test_valid_acquisition_and_checksum` | SHA256SUMS matches inventory | PASS |
| Output write failure — `test_output_write_failure_preserves_partial_evidence` | Exit 2; preserve destination and partial file | PASS (injected) |
| Output creation failure — `test_failure_after_output_creation_preserves_destination` | Exit 2; preserve destination | PASS (injected) |
| Production identity mismatch — no production identity test in this suite | Reject before acquisition | NOT TESTED (production identity intentionally not exercised) |
| Synthetic mode forbidden as root — enforced by code guard | Reject as effective UID 0 | NOT TESTED (current test runner is unprivileged; no root execution performed) |
| Output fsync error — hook exists, no dedicated test | Exit 2; preserve destination | NOT TESTED |
| Descriptor cleanup failure — no dedicated injection | Partial/nonzero with exact path/operation | NOT TESTED |

Suite totals: **PASS 20, FAIL 0, SKIP 1, NOT TESTED 4** (the four explicitly listed non-suite cases). The 21 executed unittest cases include 20 passing tests and one skip; the matrix groups some tests across multiple requirements.

## Exact verification commands and results

- `bash -n qualification/m28-3-readonly-acquisition.sh` — PASS, exit 0.
- `python3 -m py_compile tests/m28_3_readonly_acquisition_test.py` — PASS, exit 0.
- `python3 tests/m28_3_readonly_acquisition_test.py` — 20 PASS, 1 SKIP, 0 FAIL, 21 tests total. The socket case skipped because bind returned `[Errno 1] Operation not permitted`.
- `shellcheck qualification/m28-3-readonly-acquisition.sh` — SKIP; `shellcheck` is not installed.
- `git diff --check` — PASS, exit 0 for tracked changes. New deliverables are untracked, so this command does not inspect them.
- `rg -n '[[:blank:]]+$' qualification/m28-3-readonly-acquisition.sh tests/m28_3_readonly_acquisition_test.py` — PASS; no trailing whitespace found.
- No CTest command was run; this change is a standalone shell/Python qualification tool and the request did not require unrelated C++ tests.

## Status distinctions, risks, and provenance

- **IMPLEMENTED:** M28.3 files and described checks exist in source.
- **SYNTHETICALLY VERIFIED:** the nonprivileged unittest results above, including injected device/mount boundaries and injected output write failure.
- **PRODUCTION VERIFIED:** no. No production identity check or protected host path was exercised.
- **INDEPENDENTLY REVIEWED:** no. The read-only source-contract audit informed this work, but there has been no independent security approval of the completed M28.3 code.

Evidence is locally generated synthetic test output and this report. It is not signed, independently timestamped, or evidence from the approved Debian VM. The implementation remains a live, non-atomic acquisition method; races with a hostile kernel or privileged writer are outside what these tests establish. Output checksums detect later changes but do not authenticate provenance. Mount-boundary tests use safe dependency injection rather than real mount privileges. Historical absence at `/var/tmp/athena-d67-ext4` remains unknown.

## Readiness and next operator action

**Privileged acquisition readiness: NO-GO.** Require an explicit independent code review of M28.3, especially mount-ID source handling and fallback behavior, root/path classification, output fault handling, descriptor cleanup, resource-limit completeness, and the synthetic test hooks. Then perform the separately approved read-only Debian VM preflight and obtain explicit authorization before considering any privileged acquisition. Do not execute a privileged acquisition based on this report.

**Alpha M1 release readiness: NOT QUALIFIED.** No approved Debian VM campaign, privileged acceptance campaign, or evidence review was performed.

The exact next review step is an independent security/code review of `qualification/m28-3-readonly-acquisition.sh`, `tests/m28_3_readonly_acquisition_test.py`, and this report. The reviewer should resolve the NOT TESTED rows and confirm that the source-derived historical-path policy matches the authorized campaign record. Only after that review should an operator carry out the approved VM preflight and seek separate authorization.
