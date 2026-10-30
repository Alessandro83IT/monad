# Monad 0.1 Alpha M11 recovery — 2026-10-27

**Decision: BLOCKED / NO-GO. Alpha M1 remains BLOCKED.** The session stopped at Phase A as instructed because the eight M10 failures did not pass in the isolated rerun. No Phase B Arch security work or Phase C qualification work was started. No privileged test, installation, collector, Session 68 host driver, baseline/inventory/pin edit, commit, or push was performed.

## Starting state and preservation

The worktree was already heavily modified and contained untracked build/evidence directories before M11. The starting `HEAD` was `abd6bac4b368427b76f6c98522afde928b418939`; branch `main` tracked `origin/main`. Those pre-existing edits and artifacts were preserved. M10 was read at `docs/development/monad-0.1-alpha-m10-remediation-20261027.md`.

No existing files were removed or overwritten. New session artifacts are `.m11-build-20261027/` and `.m11-tmp-20261027/`; retain them as evidence/build outputs. The initial compiler attempt wrote temporary assembly to full `/tmp` and failed for lack of space. No `/tmp` cleanup was attempted.

## Capacity and fixture changes

At inspection, `/tmp` was a 3.9 GiB tmpfs with 12 MiB free (100% used) and 12,167 of 1,048,576 inodes used. `/var/tmp` and the repository filesystem were on `/dev/sda2` with 93 GiB free; inode availability there was 7,159,507 of 7,397,376. `/var/tmp` was mode `1777`. A search found no existing user-owned private directory there.

Added `tests/test_tmp_root.hpp`, which selects `ATHENA_TEST_TMPDIR` when supplied and otherwise follows `std::filesystem::temp_directory_path()` (and therefore `TMPDIR`). Updated the transaction-GC, D6 execution, D4 persistence, and D5 retention fixture roots to use this configurable root. These edits do not relax fixture assertions or production lock ancestry checks.

The repository-local temporary root was rejected by the existing trusted-ancestry check: `/home` is owned by `nobody:nogroup`. Creating a private root under `/var/tmp` would have allowed the trusted sticky `/var/tmp` ancestor, but the session sandbox permits writes only in the repository and `/tmp`. Since `/tmp` had only 12 MiB free, the eight cases could not be rerun in an eligible isolated root. The lock policy was not changed. The compile retry did succeed after setting `TMPDIR` to the repository-local isolated directory.

## Build and requested eight-test rerun

The fresh Debug build was configured in `.m11-build-20261027/`. The first build attempt failed when GCC could not write an assembly temporary under full `/tmp`. Retrying with `TMPDIR=$PWD/.m11-tmp-20261027` completed the full build successfully.

The eight requested tests were run with both `TMPDIR` and `ATHENA_TEST_TMPDIR` set to `.m11-tmp-20261027`. CTest reported all eight FAIL before reaching their fixture bodies, with `unsafe lock directory ancestor ownership or permissions`:

| Test | Result | Evidence |
|---|---|---|
| `operational_request_runtime_test` | FAIL | trusted ancestry rejected repository-local root |
| `activation_plan_persistence_test` | FAIL | trusted ancestry rejected repository-local root |
| `transaction_gc_test` | FAIL | trusted ancestry rejected repository-local root |
| `activation_plan_retention_limits_test` | FAIL | trusted ancestry rejected repository-local root |
| `activation_plan_retention_budgets_test` | FAIL | trusted ancestry rejected repository-local root |
| `activation_plan_persistence_bounds_test` | FAIL | trusted ancestry rejected repository-local root |
| `activation_plan_persistence_publication-capacity_test` | FAIL | trusted ancestry rejected repository-local root |
| `activation_plan_persistence_recovery-capacity_test` | FAIL | trusted ancestry rejected repository-local root |

No test is counted PASS or SKIP. The rerun command and detailed output are recorded in `.m11-build-20261027/Testing/Temporary/LastTest.log`.

## Phase status and closure implications

| Phase / activity | Status |
|---|---|
| A: capacity and inode diagnosis | PASS; findings above |
| A: configurable fixture roots | Partial; implemented, but isolated test root fails existing ancestry trust check |
| A: repeat the eight M10 cases successfully | FAIL: 0 PASS, 8 FAIL |
| B: Arch download, extraction, process invocation review and hardening | NOT RUN, gated by Phase A failure |
| B: hostile Arch regressions and pertinent test run | NOT RUN |
| C: broader regression run and evidence review | NOT RUN |
| Alpha M1 qualification | BLOCKED; no qualification claim |

This session added `tests/test_tmp_root.hpp` and adjusted temporary root selection in `tests/transaction_gc_test.cpp`, `tests/activation_execution_support.hpp`, `tests/activation_plan_persistence_test.cpp`, and `tests/activation_plan_retention_test.cpp`. Those paths are candidate inputs for any future M8 closure reconciliation if they are outside the recorded closure. This report does not establish the exact M8 manifest membership or propose an approved closure update. The official closure, inventories, and pins remain untouched.

## Required next step

Rerun Phase A in an authorized isolated directory on a filesystem with adequate capacity whose trusted ancestors pass the existing ownership and permission checks (for example, a provisioned private directory under `/var/tmp`), with `TMPDIR` and `ATHENA_TEST_TMPDIR` set to it. Preserve `/tmp` contents. Proceed to Phase B only after all eight cases pass. Alpha M1 remains BLOCKED.
