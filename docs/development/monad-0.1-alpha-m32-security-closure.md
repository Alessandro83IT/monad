# Monad 0.1 Alpha M1 — M32 security closure

## Executive summary

M32 is an additive read-only acquisition candidate based on M31. It closes the confirmed `/etc/os-release` pathname race and output-setup descriptor-cleanup defects, with conservative error reporting retained throughout. Synthetic regression suites pass. No production VM inspection or acquisition occurred.

**Verdict: GO_FOR_OPERATOR_REVIEW.** This means the candidate may be considered for a separately authorized diagnostic acquisition; it does not authorize execution. **Alpha M1 release readiness remains NO-GO / NOT QUALIFIED.** The approved privileged VM campaign and evidence review are outstanding.

## Baseline and preservation

- Branch: `main`; HEAD at start: `abd6bac4b368427b76f6c98522afde928b418939`.
- Source baseline: `qualification/m31-readonly-acquisition.sh`, `tests/m31_readonly_acquisition_test.py`, and `docs/development/monad-0.1-alpha-m31-final-review.md`.
- M31 and earlier artifacts were preserved. No inventory, installer, runner, pin, historical evidence, or administrative path was modified. Existing dirty worktree content was left intact.
- No sudo, privilege escalation, installer/runner execution, rollback, mount operation, systemd change, or network access occurred.

## Findings and dispositions

| Finding | Result | M32 disposition |
|---|---|---|
| Production identity TOCTOU | Confirmed. M31 used `lstat`/`realpath` followed by a pathname open, allowing the checked path to change before open. | `/` and `/etc` are opened as directory descriptors. M32 accepts a regular `/etc/os-release` opened with `O_NOFOLLOW`, or exactly the Debian `../usr/lib/os-release` symlink. In both cases the opened file's device/inode must match the validated entry. Other targets and replacement races are refused. Debian 13, hostname, machine ID, DMI serial, product UUID, and vendor predicates remain exact. |
| Output setup descriptor cleanup | Confirmed. `FileExistsError` bypassed the common cleanup branch; parent traversal could also leak a just-opened descriptor if `fstat` failed before it was tracked. | Collision and setup error paths close tracked descriptors. A newly opened parent descriptor is closed when its `fstat` fails. Failures after directory creation preserve that destination and report `OUTPUT_NOT_DURABLE`. |
| Final output identity and durability | No new critical defect confirmed. Existing M31 checks compare the output parent and output directory identity after writing; file and directory fsync, exclusive creation, short-write loops, zero-progress rejection, and checksum generation remain. | Retained. Parent/output replacement returns failure and never claims successful completion. Independent provenance authentication is not claimed. |
| Traversal error handling | M31 conservatively recorded open, enumeration, mount-ID, boundary, and disappearance failures as issues. | Retained and fault-injection coverage extended to `EMFILE`, `ENFILE`, `EACCES`, `EPERM`, and `ESTALE`. Partial traversal cannot report `COMPLETE`. |
| Atomicity/provenance | A metadata walk is inherently non-atomic; reversible changes between observations cannot be excluded. | Accepted limitation, not treated as a security failure or historical provenance proof. |

## Corrective changes and security invariants

M32 retains the fixed administrative allowlist, descriptor-relative no-follow traversal, validated synthetic-root descriptor, approved production identity values, device and mount-ID boundaries, metadata-only default, exact hash allowlist, special-file refusal, and bounded depth, entry, and byte limits. Outputs remain exclusive: directory mode 0700, file mode 0600, `SHA256SUMS`, file and directory fsync. Existing evidence is preserved on failure. Required, conditional, optional, and historical absence semantics remain distinct.

The output's final named parent and directory are rechecked against the descriptors actually used. A changed output name fails closed. Checksums provide integrity checking for the local generated files, not independent provenance authentication.

## Adversarial verification

Synthetic tests cover both accepted `/etc/os-release` layouts, trusted-target replacement between validation/open, untrusted symlinks, exact production identity constants by source assertion, output collision and setup failures, descriptor cleanup paths, output parent/directory replacement, short and zero-progress writes, file/directory fsync failures, checksum correctness, descriptor exhaustion and filesystem errors, unavailable mount IDs, filesystem boundary refusal, stable enumeration at limits, conditional/historical absence, metadata-only content policy, and synthetic-root replacement.

Tests used unprivileged filesystem fixtures and controlled fault injection. No privileged mounts were performed. The Unix socket case was skipped because the environment denied socket creation with `EPERM`.

### Test-to-requirement matrix

| Requirement | M32 evidence |
|---|---|
| Regular and symlink `os-release`; target replacement and target refusal | `test_os_release_regular_and_trusted_symlink_layouts`, `test_os_release_replacement_and_untrusted_targets_are_refused` |
| Exact production identity pins and parser | `test_production_identity_policy_pins_debian_release_symlink_target`, `test_os_release_parser_requires_unique_exact_fields` |
| Output setup errors, descriptor cleanup, destination preservation | `test_output_setup_faults_are_reported_and_destinations_preserved`, `test_existing_output_destination_rejected`, `test_failure_after_output_creation_preserves_destination`, repeated-failure descriptor test |
| Output parent/directory replacement | `test_output_parent_replacement_is_detected_without_redirection`, `test_output_directory_replacement_keeps_writes_on_open_descriptor` |
| Short/zero writes, fsync, checksums | short-write, zero-progress, fsync, and checksum assertions |
| EMFILE/ENFILE/EACCES/EPERM/ESTALE and disappearance | `test_emfile_during_traversal_is_partial`, `test_file_exists_and_errno_failures_are_conservative`, disappearance/identity test |
| Mount ID and filesystem boundaries | unavailable mount-ID and device/mount boundary tests |
| Enumeration limits, absence semantics, unauthorized reads | stable limit, conditional/historical absence, exact hash allowlist, and metadata-only tests |
| Synthetic-root replacement resistance | root path replacement and ancestor replacement tests |

## Exact verification record

All commands ran from `/home/alessandro/Projects/monad`.

| Command | Exit | Result |
|---|---:|---|
| `bash -n qualification/m32-readonly-acquisition.sh` | 0 | PASS |
| Embedded Python `compile(...)` check on the heredoc body | 0 | PASS |
| `PYTHONPYCACHEPREFIX=/tmp/monad-m32-pycache python3 -m py_compile tests/m32_readonly_acquisition_test.py` | 0 | PASS |
| `python3 tests/m32_readonly_acquisition_test.py` | 0 | 44 PASS, 1 SKIP, 0 FAIL (45 tests) |
| `python3 tests/m31_readonly_acquisition_test.py` | 0 | 40 PASS, 1 SKIP, 0 FAIL (41 tests) |
| `python3 tests/m30_readonly_acquisition_test.py` | 0 | 30 PASS, 1 SKIP, 0 FAIL (31 tests) |
| `shellcheck qualification/m32-readonly-acquisition.sh` | N/A | NOT RUN: ShellCheck is unavailable |
| `git diff --check -- qualification/m32-readonly-acquisition.sh tests/m32_readonly_acquisition_test.py docs/development/monad-0.1-alpha-m32-security-closure.md` | 0 | PASS for tracked diff; additive files are untracked |
| Python whitespace assertion over the three additive files (final newline and no trailing spaces/tabs) | 0 | PASS |

JSON schema and SHA256SUMS assertions ran in the M32 suite and passed. No timeout occurred. **Suite totals:** M32 44 PASS / 1 SKIP / 0 FAIL / 0 TIMEOUT; M31 regression 40 PASS / 1 SKIP / 0 FAIL / 0 TIMEOUT; M30 regression 30 PASS / 1 SKIP / 0 FAIL / 0 TIMEOUT. The only skipped case is Unix socket creation. ShellCheck is one NOT RUN. The synthetic outcomes are not production verification.

`git diff --check` does not include untracked files by default. The separate Python assertion checked each additive artifact for a final newline and trailing spaces/tabs; it returned 0.

## Remaining production gaps and accepted limitations

- The approved Debian VM's real identity, `/etc/os-release` layout, mount-ID availability, and actual mount/device boundaries were not checked.
- No production acquisition, privileged qualification case, evidence review, or durable-storage validation was run.
- Real kernel `ENFILE`, `EACCES`, `EPERM`, and `ESTALE` were represented with controlled synthetic faults, not induced on production paths.
- Directory metadata acquisition is non-atomic. Changes that occur and are reversed between observations may go undetected. The inventory is local and is not independently authenticated.
- ShellCheck is unavailable in this environment.

No confirmed critical or high-severity source defect remains in the reviewed candidate. These production gaps block any claim of Alpha M1 qualification, but do not block operator review of an unprivileged diagnostic candidate.

## Readiness and next operator action

- **Acquisition readiness:** `GO_FOR_OPERATOR_REVIEW`.
- **Alpha M1 release readiness:** `NO_GO` / not qualified.
- **Next operator action:** independently review the M32 source and authorize or decline a separately controlled approved-VM diagnostic acquisition. This report does not authorize execution.
