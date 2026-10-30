# Monad 0.1 Alpha M12 — D4 persistence and Arch security — 2026-10-27

**Decision: NO-GO. Alpha M1 remains BLOCKED.** Phase A was resolved and its eight requested non-privileged tests passed. Phase B Arch hardening and adversarial checks passed. This is local development evidence only; no Alpha M1 qualification is claimed.

## Starting state and preservation

At start, branch was `main`, `HEAD` was `abd6bac4b368427b76f6c98522afde928b418939`, and the worktree already contained extensive modified source, tests, reports, and build/evidence directories. All pre-existing work was treated as user work. No reset, clean, deletion, commit, push, installation, privileged test, collector, Session 68 host driver, or official inventory/baseline/pin edit was performed.

The M11 recovery report and resume report, M10 remediation report, infrastructure rules, authorization policy, and Alpha M1 recovery procedure were read. M11's final resume had 7/8 PASS and one D4 failure; it correctly stopped before Phase B.

## Phase A — D4 diagnosis and correction

M11's generic helper in `tests/activation_plan_persistence_test.cpp` threw `Expected D4 rejection` without identifying the caller. Added `std::source_location` to that helper's diagnostic while preserving the cause and outcome assertions. An isolated reproduction in the private M12 build reported the failure at the foreign-evidence substitution assertion (then line 773, cause 10, outcome 0).

The failing case was the fourth perturbation. It added an `opaque` file to a private foreign preparation directory and then expected `PlanEvidence::compare` to reject it. The production contract explicitly keeps private preparation contents opaque and rechecks the retained container identity. This was a fixture expectation defect, not a production D4 defect or an environmental precondition: the same failure reproduced with adequate space and a valid private `/var/tmp` root.

Corrected that case without weakening the security assertions: the fixture now verifies that an opaque content change is accepted while the original anchor and descriptor count remain stable, then renames/recreates the private container and requires a `Substituted` D4 rejection. The generic diagnostic now names its source call site. No production D4 behavior changed.

The initially reproduced run was **FAIL** (35.66 s); the isolated persistence test after correction was **PASS** (45.40 s). The final M11 eight-test rerun is below.

| M11 test | Result | Duration |
|---|---:|---:|
| `operational_request_runtime_test` | PASS | 2.61 s |
| `activation_plan_persistence_test` | PASS | 45.45 s |
| `transaction_gc_test` | PASS | 1.14 s |
| `activation_plan_retention_limits_test` | PASS | 7.01 s |
| `activation_plan_retention_budgets_test` | PASS | 33.59 s |
| `activation_plan_persistence_bounds_test` | PASS | 11.80 s |
| `activation_plan_persistence_publication-capacity_test` | PASS | 4.55 s |
| `activation_plan_persistence_recovery-capacity_test` | PASS | 10.84 s |

Final M11 batch: **8 PASS, 0 FAIL, 0 SKIP, 0 TIMEOUT**, 117.00 s. Evidence: `/var/tmp/monad-alpha-tests/m12-20261027-213118/phase-a-final-eight-tests.log`.

## Phase B — Arch backend security

The Arch backend no longer constructs shell commands or calls `system`, `popen`, or `pclose`:

- Repository database and package downloads invoke `curl` with direct argv through `athena::process::run`, terminate option parsing with `--`, use a unique `mkdtemp` directory (`0700`) beside the destination, and atomically rename a complete download into place. Failed downloads leave an existing destination intact.
- Detached signature downloads use the same private staging approach; `gpgv` receives direct argv and a fixed keyring path. Package/keyring inputs must be regular non-symlink files.
- Arch SHA-256 verification uses the in-process OpenSSL verifier. The verifier opens with `O_NOFOLLOW|O_NONBLOCK`, checks the descriptor is a regular file, and hashes a bounded stream.
- Repository databases are read in one LibArchive pass from an `O_NOFOLLOW` regular-file descriptor. The reader validates tar format, safe normalized member paths, duplicate and file/directory collisions, types, declared sizes, metadata size, corruption, and close status. It no longer lists and reopens the archive through tar or parses newline-delimited command output.
- Package extraction creates a new private destination (`0700`) and writes members through directory FDs with `openat`, `O_NOFOLLOW`, and `O_EXCL`. It rejects absolute/traversal/backslash paths, symlinks, hardlinks, special files, privileged mode bits, duplicate names, file/directory collisions, truncated/corrupt entries, and pre-existing destinations. Any failure removes the extraction tree created by that call. Files and directories receive sanitized modes; no archive ownership is restored.
- Resource ceilings are 100,000 archive entries, 8 GiB total expanded data, 2 GiB per entry, and 16 MiB per database `desc` record.

`tests/arch_repository_security_test.cpp` adds hostile package and database archives covering traversal, absolute/backslash paths, symlink/hardlink/special entries, duplicates, path collisions, oversized entries/metadata, corrupt archives, archive symlink inputs, and cleanup after rejection. It also checks local-file downloads, atomic preservation after a failed download, valid/mismatched SHA-256, and rejection of a symlink keyring/package. Existing process invocation and M3 archive-security tests were included.

The final targeted CTest run was **7 PASS, 0 FAIL, 0 SKIP, 0 TIMEOUT**, 4.08 s:

| Test | Result |
|---|---:|
| `arch_repository_test` | PASS |
| `arch_repository_security_test` | PASS |
| `arch_repository_adapter_test` | PASS |
| `arch_version_test` | PASS |
| `process_invocation_test` | PASS |
| `archive_security_test` | PASS |
| `installer_e2e_test` | PASS |

Evidence: `/var/tmp/monad-alpha-tests/m12-20261027-213118/phase-b-final-tests.log`. A source scan found no shell invocation or shell command construction in `src/repository/arch_repository.cpp`.

## Phase C — isolated build and qualification scope

The isolated Debug build was configured and built at `/var/tmp/monad-alpha-tests/m12-20261027-213118/build`; the final full build completed successfully. Compiler output included an existing unrelated `nodiscard` warning in `tests/store_metadata_errors_test.cpp`. Build and fixture roots were below `/var/tmp/monad-alpha-tests`; no `/tmp` build or fixture root was used.

At the final capacity check, `/var/tmp/monad-alpha-tests`, the M12 root, build directory, and temporary root were owned by `alessandro:alessandro`, mode `0700`. The filesystem had 92 GiB free and 7,155,881 free inodes. The full CTest suite was **NOT RUN**. No privileged case, live Arch/network test, real installation, collector, or Session 68 host driver was run.

The M8 source-closure proposal and official pins/inventories/baselines were not changed. The M8 closure is invalid for this source state: the M12 changes alter closure members `CMakeLists.txt`, `src/repository/arch_repository.cpp`, `src/repository/arch_repository.hpp`, `src/verify/verifier.cpp`, and `tests/activation_plan_persistence_test.cpp`; `tests/arch_repository_security_test.cpp` is a new file under the M8 `tests` root but is absent from its 424-entry manifest. The verifier had already been changed in M10; M12 modifies its no-follow behavior further. A reviewed closure reconciliation is still required; no digest or pin was regenerated.

## Residual limits and status

- Extraction deliberately rejects all symlinks and hardlinks, as the M3 extractor does. Some valid Arch packages contain links, so this safe policy can reject otherwise legitimate packages; a separately reviewed, containment-safe link policy would be needed before claiming broad package compatibility.
- The configured entry-count and aggregate expansion ceilings are implemented; adversarial tests exercise per-entry and metadata size rejection, but do not stress the 100,000-entry or 8 GiB aggregate boundary.
- This run did not validate a cryptographically valid OpenPGP package signature, exercise live remote mirrors, or qualify recovery after power loss. The full suite and approved Debian VM privileged campaign remain outstanding.
- M8 source-closure reconciliation, inventory/pin review, independent qualification evidence review, all required privileged cases, and Debian VM campaign remain open. Alpha M1 stays **BLOCKED**.

## Result classification

| Scope | PASS | FAIL | SKIP | TIMEOUT | NOT RUN |
|---|---:|---:|---:|---:|---|
| Initial isolated D4 reproduction | 0 | 1 | 0 | 0 | — |
| Corrected isolated D4 persistence test | 1 | 0 | 0 | 0 | — |
| Final M11 eight-test batch | 8 | 0 | 0 | 0 | — |
| Final Arch/process/archive/installer batch | 7 | 0 | 0 | 0 | — |
| Full CTest suite | — | — | — | — | NOT RUN |
| Privileged tests / VM campaign | — | — | — | — | NOT RUN |
| Live Arch, installation, collector, Session 68 driver | — | — | — | — | NOT RUN |

`git diff --check` passed. No commit or push was made.

**GO/NO-GO: NO-GO. Alpha M1 remains BLOCKED.**
