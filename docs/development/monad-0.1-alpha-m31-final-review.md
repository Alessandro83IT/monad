# Monad 0.1 Alpha M31 final hardening review

## Decision

**GO_FOR_OPERATOR_REVIEW.** The M31 candidate has no confirmed critical or high-severity defect remaining in the reviewed source and unprivileged synthetic checks. This verdict allows a separate review of whether to authorize an acquisition; it does not authorize collector execution. **Alpha M1 remains NOT QUALIFIED and is not release-ready.** No approved Debian VM campaign or evidence review occurred in this session.

## Executive summary

M31 is an additive candidate based on `qualification/m30-readonly-acquisition.sh`; M30, M29 and previous evidence remain unchanged. M31 closes confirmed defects in output descriptor cleanup, enumeration-budget interpretation, partial inventory schema consistency, source-role classification, trusted `/etc/os-release` handling, output parent replacement detection, and error reporting. It retains descriptor-relative no-follow traversal, the synthetic root descriptor, fixed acquisition scope, exact production identity constants, device and mount-ID checks, metadata-only content policy, exact hash allowlist, special-file refusal, bounded traversal, exclusive 0700/0600 output, SHA256SUMS and fsync requirements.

The unprivileged M31 suite reports **40 PASS, 1 SKIP, 0 FAIL** (41 tests). The M30 suite reports **30 PASS, 1 SKIP, 0 FAIL** (31 tests); M29 reports **27 PASS, 1 SKIP, 0 FAIL** (28 tests). Each skip is the Unix socket fixture denied by the sandbox. No privileged or mount tests were run.

## Source baseline and preservation

- Branch: `main`
- HEAD: `abd6bac4b368427b76f6c98522afde928b418939`
- Baseline: M30 collector, its tests and qualification report.
- The worktree contained extensive pre-existing modifications, evidence, and build directories. They were treated as user work. Only the M31 collector, M31 tests and this review were added for this request. M30 and earlier artifacts were not modified. No cleanup, reset, commit, push, installer or runner execution, privileged operation, mount operation, or network access occurred.

## Findings and dispositions

| Concern | Finding / severity | M31 disposition |
|---|---|---|
| A. Output descriptor lifetime and post-`mkdirat` failures | Confirmed medium reliability defect. M30's setup error branches could leave locally opened descriptors to interpreter shutdown; output close errors also escaped the output error classification. The process exit prevented a persistent cross-run descriptor leak, so this was not a privilege-boundary escape. | Explicitly closes descriptors on setup failure and on all output paths; close failures are reported as `OUTPUT_NOT_DURABLE`. Created destinations are preserved for review. |
| B. Descriptor exhaustion in recursion | Confirmed bounded availability risk, not an escape. Recursive descent holds parent directory descriptors; the hard depth limit bounds the maximum. M30 handled many open errors but did not directly exercise injected `EMFILE`. | `EMFILE` and other open errors become per-path partial evidence; synthetic injected `EMFILE` and low `RLIMIT_NOFILE` tests pass. |
| C. Enumeration budget vs mutation detection | Confirmed. M30 shared enumeration accounting between first and second snapshots. When the budget was exhausted, a stable directory could be called changed or incompletely verified without a clear distinction. | Initial snapshots reserve one-item look-ahead. Verification is sized from the first snapshot; a truncated first pass is labeled incomplete rather than mutated. Stable directories at the entry limit are tested. |
| D. Complete/partial schema mismatch | Confirmed. M30 emitted schema 4 for complete inventories and schema 3 after output-size fallback. | Both full and size-limited inventories use schema 4 and the same top-level fields; the fallback omits detailed result rows and records that omission. |
| E. Target classification | Confirmed. A single required/optional split incorrectly treated conditional and historical targets as universal blockers. | Records now label required installed artifacts, prerequisites, conditional state/input/receipt/fixture paths, optional runtime state and historical-only evidence. A historical target's current absence never proves historical absence. |
| F. Debian `/etc/os-release` layout | Confirmed production compatibility issue. M30's `O_NOFOLLOW` open rejects the legitimate Debian symlink layout. | A regular file is opened no-follow. A symlink is accepted only when `realpath` is exactly `/usr/lib/os-release`, then that fixed target is opened no-follow. All exact Debian 13 and VM identity constants remain unchanged. Policy is source-asserted synthetically; actual production identity is not tested here. |
| G. Root anchoring and pathname replacement | Root pathname replacement was already mitigated by M30's retained synthetic-root descriptor; regression confirms traversal still uses it. M30 did not verify that the named output parent remained the originally opened directory. | Root replacement and output directory replacement remain descriptor-anchored. M31 reopens the output parent one no-follow component at a time and compares descriptor identity before success. Replacement is detected and evidence stays at the anchored location. |
| H. Durability, integrity and cleanup | M30's file and directory fsync, exclusive output creation, checksum and preservation behavior were present. Parent-name replacement and close failures were not fully represented. | Retains exclusive creation, complete short-write loops, SHA256SUMS and file/directory fsync. Zero-progress, fsync, write, directory replacement and parent replacement failures return output failure and preserve artifacts; no cleanup deletes evidence. |
| I. Filesystem errors and PARTIAL reporting | M30 caught the common traversal and hash errors, but descriptor close failures could escape and not every resource error was observable in tests. | Per-path filesystem/resource errors and mutations set PARTIAL. Output creation/write/fsync/close failures use `OUTPUT_NOT_DURABLE`; identity mismatch uses `IDENTITY_REFUSED`; unsafe parent/scope conditions use `SECURITY_BOUNDARY_REFUSED`. No broad exception handling converts programming errors into success. |

### Rejected or downgraded concerns

- The output descriptor is not a lasting leak after the M30 process exits. It was a confirmed cleanup and classification defect, not evidence that another invocation inherited its descriptors.
- M30 already anchored synthetic-root traversal with a retained descriptor and `O_NOFOLLOW` component opens. The root TOCTOU allegation is not a new M30 defect; M31 preserves it and expands observable regression assertions.
- Concurrent acquisition is not atomic. A mutation that occurs and is reversed entirely between observations can escape detection. This is an inherent limitation of the documented metadata walk, not a claim of snapshot consistency; visible errors, identity changes, and directory changes become PARTIAL.
- The enumeration budget concern is downgraded from “stable directory reported changed” for the relevant exhaustion path to “incomplete verification” in M31. The global entry limit still bounds visited entries and each verification pass is bounded by observed entries plus a one-item look-ahead.

## Security invariants

- Synthetic traversal begins from a stable root directory descriptor and opens each component with directory/no-follow flags.
- Production traversal begins at `/`; fixed administrative paths are used. No arbitrary environment-provided production scope is accepted.
- Production identity remains pinned to Debian 13, hostname, machine ID, VirtualBox serial/product UUID and approved vendor values. No identity check was weakened.
- Root device and mount ID are compared throughout descent. Missing mount identity or a boundary is partial and descent stops at that object.
- Regular-file content remains unread unless the exact authorized lock path matches the allowlist; special files are not opened.
- Traversal and hashing have default and hard depth, entry and byte caps. Output directory/file permissions remain 0700/0600.
- Output and acquisition names are created exclusively and through validated descriptors. No arbitrary symlink is followed.
- A failed output is preserved and is not represented as durable evidence. A complete record is local, non-atomic and not independently authenticated.

## Source-derived path contract matrix

The roles below follow `qualification/install-athena-alpha-m1-admin.sh` and `qualification/athena-alpha-m1-runner.py`. “Blocker” is phase-dependent: collector absence classification does not infer that every installation or fixture phase is active.

| Acquisition path | Source-defined role | Is absence expected? | Absence blocks qualification? | Historical-only? | Collector traversal |
|---|---|---:|---|---:|---|
| `/usr/local/sbin/athena-alpha-m1-runner` | Required installed artifact | No after installation | Yes for installed-artifact review | No | Metadata only; no symlink following |
| `/usr/local/sbin/gateway_support.py` | Required installed artifact | No after installation | Yes for installed-artifact review | No | Metadata only; no symlink following |
| `/etc/sudoers.d/athena-alpha-m1` | Required installed artifact | No after installation | Yes for installed-artifact review | No | Metadata only; no symlink following |
| `/var/lib/athena-alpha-m1/admin-install.lock` | Conditional installer lock state | Yes before lock acquisition / in unused state | Only when the relevant admin transaction is in scope | No | Exact empty-file path alone may be hashed; otherwise metadata only |
| `/var/lib/athena-alpha-m1` | Required protected state prerequisite | No for installed review | Yes | No | Metadata-only walk, bounded |
| `/var/lib/athena-alpha-m1/backups` | Required protected backup root | No for installer prerequisite | Yes for installer prerequisite review | No | Metadata-only walk; backup children are conditional |
| `/root/athena-alpha-m1-stage` | Conditional installer input | Yes after staging cleanup / outside install preparation | Only for an install-preparation phase | No | Metadata only; no content reads |
| `/run/athena-alpha-m1` | Optional volatile runner state | Yes; runner creates it on demand | No by itself | No | Metadata-only walk when present |
| `/var/log/athena-alpha-m1` | Required installed directory | No after installation | Yes for installed-artifact review | No | Metadata-only walk; no log contents read |
| `/var/lib/athena-alpha-m1/systemd-canary.json` | Conditional fresh install precondition | Yes outside a fresh canary/install phase | Only when that phase is applicable | No | Metadata only; receipt content is not read |
| `/var/lib/athena-alpha-m1/ext4-fixture` | Conditional fixture output | Yes when fixture preparation was not run | Only for the fixture qualification phase | No | Metadata only; no fixture content reads |
| `/var/tmp/athena-d67-ext4` | Historical residual/evidence target | Yes currently, but current absence says nothing about prior existence | Never inferred from absence; separate historical evidence governs | Yes | Metadata only when present; preserve residuals |
| `/opt/athena-qualification/build` | Authenticated installer input | No for installer preparation | Yes for installer preparation | No | Metadata-only acquisition; exact file allowlist is unchanged |

Required current roots/installed outputs absent at acquisition time produce a blocker record. Conditional and historical targets produce explicit non-universal statuses; missing intermediate parents and access failures are not treated as leaf absence.

## Test-to-requirement matrix

| Requirement | M31 synthetic coverage |
|---|---|
| Synthetic root replacement, stable root descriptor identity, ancestor symlink resistance | `test_root_path_replacement_does_not_redirect_traversal`, `test_ancestor_symlink_replacement_does_not_escape_root_descriptor`, ancestor/descendant symlink tests |
| Output mkdir failure, descriptor lifetime, output directory and parent replacement | `test_failure_after_output_creation_preserves_destination`, `test_repeated_output_failures_do_not_leak_descriptors_to_caller`, output directory replacement, output parent replacement tests; setup and final descriptor close paths reviewed |
| `EMFILE`, low descriptor limit and bounded traversal | `test_emfile_during_traversal_is_partial`, `test_low_descriptor_limit_and_deep_tree_are_partial_without_crash`, depth/entry/hash limits |
| Enumeration budget and mutation | `test_stable_directory_at_entry_limit_is_not_called_changed`, entry-limit and file/directory mutation tests |
| Required, optional, conditional and historical absence | required absence, optional leaf/ancestor, conditional lock/canary and historical target tests |
| Complete/partial schema and output checksums | size-limited schema test; valid and short-write checksum assertions |
| File/directory fsync, short and zero-progress writes | fsync failure and short-write tests; `test_zero_progress_write_is_not_reported_durable` |
| Unauthorized content-read prevention and exact hash allowlist | metadata-only large file test and exact lock-file hash test |
| Production identity parsing / trusted release path policy | `test_os_release_parser_requires_unique_exact_fields` exercises quoted/unquoted exact fields and rejects near-match/duplicate keys; source-contract test confirms Debian pins and regular-file/trusted-target/no-follow policy. Production values and actual symlink behavior NOT TESTED on approved VM |
| Mount/device boundary and special files | injected boundary/missing-mount-ID tests; FIFO test. Real mount transitions NOT TESTED; Unix socket fixture SKIPPED due sandbox `EPERM` |

## Verification record

Commands and results from this session:

| Command | Exit | Result |
|---|---:|---|
| `bash -n qualification/m31-readonly-acquisition.sh` | 0 | PASS |
| `PYTHONPYCACHEPREFIX=/tmp/monad-m31-pycache python3 -m py_compile tests/m31_readonly_acquisition_test.py` | 0 | PASS |
| Embedded collector Python `compile(...)` check | 0 | PASS |
| `python3 tests/m31_readonly_acquisition_test.py` | 0 | 40 PASS, 1 SKIP, 0 FAIL (41 tests) |
| `python3 tests/m30_readonly_acquisition_test.py` | 0 | 30 PASS, 1 SKIP, 0 FAIL (31 tests) |
| `python3 tests/m29_readonly_acquisition_test.py` | 0 | 27 PASS, 1 SKIP, 0 FAIL (28 tests) |
| `shellcheck qualification/m31-readonly-acquisition.sh` | N/A | NOT RUN: ShellCheck unavailable |
| `git diff --check` | 0 | PASS for tracked changes; new deliverables were also checked in the final review below |

The Unix socket synthetic case was SKIPPED in each suite because socket creation returned `EPERM`. No test timed out. Checksums are asserted against each generated inventory in valid and short-write test cases. Full and size-limited JSON top-level schema consistency is asserted. M31 totals: **40 PASS / 1 SKIP / 0 FAIL / 0 TIMEOUT / 0 NOT RUN within the synthetic test suite**. Tool totals across all verification: ShellCheck **1 NOT RUN**, all other listed checks PASS. No test status is represented as production evidence.

## Production verification gaps and residual risks

- Exact approved Debian VM identity, real `/etc/os-release` layout, DMI identity, mount-ID availability and actual device/mount boundaries were not tested.
- No production acquisition or privileged campaign was performed. This is intentionally not a prerequisite for deciding whether source is safe enough for operator review.
- Real storage durability, ACL/ownership behavior, and `fsync` semantics on the approved VM filesystem remain unverified.
- Directory acquisition is non-atomic; a mutation reversed between observations may not be detected. Inventory hashes only the authorized exact empty lock file and is not independently authenticated.
- Low `RLIMIT_NOFILE` was exercised synthetically; kernel-specific `ENFILE`, real `EACCES`/`EPERM` and `ESTALE` conditions were not forced.
- M31 still needs independent source review and, if separately authorized, an approved VM preflight before any acquisition.

## Readiness and next action

- **Acquisition readiness:** GO_FOR_OPERATOR_REVIEW.
- **Alpha M1 release readiness:** NO-GO / NOT QUALIFIED. The approved privileged campaign and evidence review remain outstanding.
- **Next operator action:** review this M31 candidate and authorize or decline a separate approved Debian VM preflight/acquisition request. This report does not authorize execution.
