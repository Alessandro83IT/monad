# Monad 0.1 Alpha M11 — Phase A resumption — 2026-10-27

**Decision: NO-GO. Alpha M1 remains BLOCKED.** This resume completed the requested non-privileged rerun of the eight M10/M11 cases using `/var/tmp/monad-alpha-tests`. Seven passed and one failed. The failure was not localized to a specific assertion, so no code change was justified. Phase B was not started because Phase A did not pass. No privileged test, real installation, Alpha M1 collector, Session 68 host driver, baseline/inventory/official pin change, commit, or push was performed.

## Starting state and preservation

The prior M11 report was read in full at `docs/development/monad-0.1-alpha-m11-recovery-20261027.md`, along with M10 and the infrastructure, authorization, and Alpha M1 recovery rules it references. The initial branch was `main`, `HEAD` was `abd6bac4b368427b76f6c98522afde928b418939`, and the worktree already contained extensive modified and untracked source, test, report, and build artifacts. Those changes were treated as pre-existing and were not edited in this resume. This report is a new file; previous reports and external fixtures were not intentionally removed or overwritten.

## Temporary root capacity and permissions

The requested `/var/tmp/monad-alpha-tests` already existed, owned by `alessandro:alessandro` (`uid=1000`, `gid=1000`), mode `0700`. `/var/tmp` was owned by `nobody:nogroup`, mode `1777`. The underlying `/dev/sda2` filesystem had 93 GiB available and 7,157,740 free inodes at inspection. No `/tmp` fixture or build was used.

A new isolated directory, `/var/tmp/monad-alpha-tests/m11-resume-20261027`, was created with owner `alessandro:alessandro` and mode `0700`. The CTest command set both `TMPDIR` and `ATHENA_TEST_TMPDIR` to this directory. The completed CTest output was captured at `/var/tmp/monad-alpha-tests/m11-resume-20261027/phase-a-eight-tests.log`.

## Eight requested tests

The cases were run from the existing `.m11-build-20261027` Debug build. The results were:

| Test | Result | Duration |
|---|---|---:|
| `operational_request_runtime_test` | PASS | 2.40 s |
| `activation_plan_persistence_test` | FAIL — emitted `Expected D4 rejection` after its R2/R1 diagnostic sections | 35.50 s |
| `transaction_gc_test` | PASS | 1.35 s |
| `activation_plan_retention_limits_test` | PASS | 7.06 s |
| `activation_plan_retention_budgets_test` | PASS | 34.25 s |
| `activation_plan_persistence_bounds_test` | PASS | 11.91 s |
| `activation_plan_persistence_publication-capacity_test` | PASS | 4.61 s |
| `activation_plan_persistence_recovery-capacity_test` | PASS | 10.64 s |

CTest summary: 7 PASS, 1 FAIL, 0 SKIP, 0 TIMEOUT; total 107.72 s. The failing output identifies the helper `error()` in `tests/activation_plan_persistence_test.cpp`, which throws when an expected `PlanPersistenceError` was not observed. It does not identify which call site accepted the state. The logged diagnostics show the earlier R2-B4, R2-B3, R2-B2, and R1 sections completed, but that is insufficient to establish which later D4 case failed or to justify a production fix. No assertion or security check was weakened. No verified correction was available to apply, and the eight-test batch was not repeated after a change.

The standalone persistence case was also started once with a fresh private fixture directory, but its process result could not be retrieved from the execution session; it is therefore **NOT COUNTED** as PASS, FAIL, SKIP, or TIMEOUT. The authoritative recorded result remains the CTest failure above.

## Phase status

| Phase / activity | Status |
|---|---|
| A: temporary filesystem owner/mode, capacity, inode inspection | PASS |
| A: isolated fixture-root selection | PASS; all eight tests used the requested `/var/tmp` hierarchy |
| A: eight-test rerun | FAIL; 7 PASS, 1 FAIL |
| A: failure analysis | Incomplete; generic D4 rejection diagnostic does not identify call site; no demonstrated safe fix |
| A: repeat after correction | NOT RUN; no correction was made |
| B: Arch shell invocation removal, LibArchive protections, adversarial tests/build | NOT RUN; gated on Phase A PASS |
| Privileged tests / installation / collector / Session 68 driver | NOT RUN, per constraints |
| Alpha M1 qualification | BLOCKED; no qualification claim |

CTest updated the existing build directory's `Testing/Temporary/LastTest.log` during this run. The complete output from this resume is separately retained at the external log path above. No cleanup was performed in the build directory or under `/var/tmp`.

## Modified files and residual risks

This resume added only `docs/development/monad-0.1-alpha-m11-resume-20261027.md`. It did not edit source, tests, build configuration, official inventories, baselines, or security pins. The Phase A failure remains unresolved; consequently, no Arch hardening work or corresponding adversarial verification was performed. Existing M10/M11 security and source-closure risks remain as documented in the historical reports.

**GO/NO-GO: NO-GO. Alpha M1 remains BLOCKED.** Phase B requires a future Phase A run in which all eight cases pass and the D4 failure is diagnosed from a specific assertion with supporting evidence.
