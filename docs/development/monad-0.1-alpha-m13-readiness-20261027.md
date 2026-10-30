# Monad 0.1 Alpha M13 — integration and release-candidate readiness

**Decision: NO-GO. Alpha M1 remains BLOCKED.** This report records local development and test evidence only. It does not qualify, approve, publish, or authorize installation of an Alpha candidate.

Execution took place on 2026-10-28; the report filename uses the requested M13 date tag `20261027`.

## Scope and preservation

The starting branch was `main`, `HEAD` was `abd6bac4b368427b76f6c98522afde928b418939`, and the worktree already contained numerous user-owned source edits, untracked files, reports, fixtures, and build directories. They were preserved. No reset, clean, commit, push, official inventory/baseline/pin promotion, privileged test, real system installation, Alpha M1 collector, or Session 68 host driver was run.

The complete M8, M9, M10, M11 recovery/resume, and M12 reports were reviewed. Historical outcomes are not added to M13 counts: M12's eight D4 cases and seven Arch/process/archive tests remain historical results. M13 evidence is identified separately below.

Build directories and logs, and all fixtures in the final compliant run, were under `/var/tmp/monad-alpha-tests`; the session directory was `/var/tmp/monad-alpha-tests/m13-20261028-BdVeKZ`, owner `alessandro:alessandro`, mode `0700`. The build directory was inside it. `TMPDIR` was `/var/tmp/monad-alpha-tests`; final CTest runs set both `TMPDIR` and `ATHENA_TEST_TMPDIR` to the private M13 directory. The earlier exploratory fixture-placement exception is disclosed below.

**Run correction:** an earlier exploratory parallel CTest invocation exposed that seven selected C++ tests still hardcoded fixture patterns under `/tmp`. I stopped treating that invocation as final evidence, did not inspect or clean `/tmp`, and changed those fixture roots to use `ATHENA_TEST_TMPDIR`: `activation_digest_test`, `activation_projection_fs_test`, `durable_filesystem_test`, `generation_membership_test`, `initial_activation_test`, `legacy_d3_test`, and `locking_test`. Their assertions and cleanup behavior were left intact. The final CTest run below used the corrected helpers. The exploratory run is retained at `/var/tmp/monad-alpha-tests/m13-20261028-BdVeKZ/ctest-nonprivileged.log` and is not included in final counts.

## Phase A — integration

### Build and static checks

| Check | Result | Evidence |
|---|---|---|
| Fresh CMake Debug configure | PASS | `configure.log` in the private M13 directory |
| Full Debug build after M13 changes | PASS | `final-build.log`; `cmake --build ... --parallel 4` |
| `git diff --check` | PASS | checked after source/test edits |
| CTest discovery | PASS: 150 registered tests | `ctest-inventory.json` |
| Final nonprivileged CTest batch | 111 PASS, 0 FAIL, 0 SKIP, 4 TIMEOUT | `ctest-final-nonprivileged.log`; serial, 115 selected |
| Additional nonprivileged Alpha/qualification unit tests | 4 PASS, 1 FAIL, 0 SKIP, 0 TIMEOUT | `ctest-alpha-unit-tests.log`; 5 selected |

The final tested set therefore totals **115 PASS, 1 FAIL, 0 SKIP, 4 TIMEOUT across 120 selected cases**. Thirty of the 150 registered tests were intentionally not run by this development session. No historical M8–M12 results are included in these totals.

### Explicit exclusions

The main batch used `ctest --output-on-failure -LE privileged -E '(scanner|collector|alpha_m1_|qualification_driver)'`. It ran serially to avoid I/O contention between exhaustive persistence tests. The five additional unit tests below were run separately after confirming their behavior is mocked/static and does not invoke a host driver.

All **25 privileged-labelled tests** were excluded:

`installed_semantics_integration_test`, `cli_install_integration_test`, `install_transaction_integration_test`, `generation_activation_test`, `realization_executor_integration_test`, `removal_realization_integration_test`, `immutable_object_publication_test`, `activation_execution_privileged_sources_test`, `activation_execution_privileged_legacy_test`, `activation_execution_privileged_owner_test`, `activation_execution_privileged_bind_test`, `activation_execution_privileged_nested_test`, `activation_execution_privileged_ext4_test`, `activation_plan_retention_privileged_owner_test`, `activation_plan_retention_privileged_bind_test`, `activation_plan_retention_privileged_nested_test`, `activation_plan_retention_privileged_ext4_test`, `activation_plan_persistence_privileged_test`, `legacy_d3_scanner_test`, `activation_plan_persistence_privileged_owner_test`, `activation_plan_persistence_privileged_bind_test`, `activation_plan_persistence_privileged_nested_test`, `activation_plan_persistence_privileged_tmpfs_test`, `activation_plan_persistence_privileged_ext4_test`, and `initial_activation_migration_test`.

All five unlabelled scanner targets were excluded: `object_store_scanner_test`, `artifact_identity_scanner_test`, `artifact_scanner_test`, `transaction_gc_scanner_test`, and `package_operation_scanner_test`. No collector test was registered in this CTest inventory. No Session 68 host-driver test was registered or invoked. `package_operation_test`, which did run, uses a simulated scanner fixture; it does not run the production scanner or a privileged host scan.

The five supplementary unit tests were `qualification_driver_test`, `alpha_m1_canary_child_test`, `alpha_m1_canary_timeout_test`, `alpha_m1_admin_static_test`, and `alpha_m1_orchestrator_test`. The four `alpha_m1_*` tests passed with local/static mocks; they did not execute the canary, collector, privileged runner, or host driver.

### Test outcomes and remaining failures

The main 115-case run completed in 3269.85 seconds:

- **111 PASS**.
- **0 FAIL**.
- **0 SKIP**.
- **4 TIMEOUT**, each retaining its configured CTest limit of 600 seconds: `activation_selection_test`, `activation_plan_persistence_boundaries_fresh_test`, `activation_plan_persistence_boundaries_bootstrap_test`, and `activation_plan_persistence_boundaries_reused_test`.
- **0 NOT RUN within the selected batch**; the 35 cases excluded for privilege/scanner/tooling reasons are reported separately above.

`activation_selection_test` also timed out at 600 seconds in an isolated CTest diagnostic run. A short direct diagnostic with temporary stage messages showed it progressing through bootstrap, transitions, invalid-input cases, fsync fault cases, and into the 141-boundary I/O fault tests; the temporary messages were removed. This is evidence of excessive test runtime, not evidence that its assertions passed. No timeout limit or test assertion was changed.

The three persistence boundary cases each enumerated approximately 10.5k–11.4k fault points and did not finish within their existing 600-second CTest limit. `activation_plan_persistence_boundaries_published_test` completed **PASS** in 563.24 seconds, including its published and recovery subpaths.

The additional five unit tests produced:

| Test | Result | Detail |
|---|---|---|
| `qualification_driver_test` | FAIL | Seven subtests errored before their assertions because the current qualification source inventory omits 16 present inputs; list below |
| `alpha_m1_canary_child_test` | PASS | Static child-program checks only |
| `alpha_m1_canary_timeout_test` | PASS | Mocked clock and subprocess calls only |
| `alpha_m1_admin_static_test` | PASS | Static allowlist/pin consistency checks; no installation |
| `alpha_m1_orchestrator_test` | PASS | Subprocess calls mocked; no `sudo` execution |

The 16 unlisted inputs reported by `qualification_driver_test` are `src/process/run.cpp`, `src/process/run.hpp`, nine Alpha M1 tooling tests (`alpha_m1_collector_v2_test.py`, `alpha_m1_collector_v2_trust_test.py`, `alpha_m1_ctest_inventory_test.py`, `alpha_m1_evidence_tree_digest_test.py`, `alpha_m1_installer_transaction_test.py`, `alpha_m1_receipt_path_test.py`, `alpha_m1_verified_evidence_runner_test.py`, `alpha_m1_verified_execution_bootstrap_test.py`, `alpha_m1_vm_evidence_collector_test.py`), plus `tests/arch_repository_security_test.cpp`, `tests/archive_security_test.cpp`, `tests/monad_build_attest_test.py`, `tests/process_invocation_test.cpp`, and `tests/test_tmp_root.hpp`. No official qualification inventory was changed to hide this failure.

`installer_e2e_test` and packaging tests in the main batch use temporary fixture/build paths; they did not install into the host system. No real installation was performed.

## Phase B — Arch symlink and hardlink behavior

The M12 implementation safely rejected all package symlinks and hardlinks. That blocked ordinary Arch packages using relative symlinks, which are a normal way to expose executable/library names. M13 adds safe extraction support and adversarial tests in `src/repository/arch_repository.cpp` and `tests/arch_repository_security_test.cpp`.

The extractor now:

- accepts relative symlinks only; resolves link chains against archive-root-relative paths and rejects absolute targets, escapes, cycles, malformed targets, and path collisions;
- validates all symlink targets before creating any symlink, and creates symlinks after regular members so archive entries cannot write through a link;
- accepts a hardlink only when its normalized target is an earlier extracted regular file, then creates it with descriptor-relative `linkat`; it rejects forward targets, traversal, missing targets, links to symlinks, and special-file targets;
- keeps the private extraction root, no-follow directory traversal, exclusive file creation, sanitized modes, archive limits, rejection cleanup, and no ownership restoration from M12.

The targeted CTest `arch_repository_security_test` is **PASS** after the correction. The first broad run found a hardlink header whose LibArchive file type is zero; the check was corrected to accept that type only when the hardlink field is present, and the final broad run passed the Arch security test. Hostile cases include absolute/traversal names, external or chained-escape symlinks, symlink cycles, hardlink traversal/forward targets, special files, duplicates/collisions, oversized/corrupt entries, and archive symlink inputs. Valid relative symlink and in-archive hardlink extraction also pass, with the hardlink inode relationship checked.

**Accepted Alpha limitation:** hardlinks to later archive members or to other links remain unsupported; the package is rejected before extraction completes. This is a bounded compatibility limitation, not a path to outside writes. Database links remain rejected because repository `desc` metadata is expected as regular files/directories. A valid relative symlink whose target is absent is not rejected solely for being dangling. Broad compatibility against a corpus of signed live Arch packages was not tested; no live mirror, OpenPGP verification, or host package installation was attempted.

## Phase C — local source closure and artifacts

M8 closure manifest: `docs/development/monad-0.1-alpha-m8-source-closure-20261027.json`, SHA-256 `8ff98f55fafd85808228a97e878e44b9284325364e774bfaac8d1005b8a00d4c`. The digest was recomputed and matches M8.

The separate candidate inventory is [`monad-0.1-alpha-m13-source-closure-candidate-20261028.json`](monad-0.1-alpha-m13-source-closure-candidate-20261028.json), SHA-256 `962b9148e2c15d4762835aef21a7dda85accf9c8999c54dbe1f32395bc264b40`. Its comparable M8 technical scope records **426 regular-file inputs**, each with SHA-256 and byte size. All 426 hashes and sizes were recomputed against the current worktree with zero mismatch.

To cover qualification code that M8 kept outside its build-source roots, the M13 JSON also records a separate scope of **49 qualification/process source files** under `qualification/`, `scripts/`, and `tools/`, plus **14 named context documents** covering M8–M12, authorization, infrastructure, and the Debian VM procedure. These entries have local hashes and sizes, but are not added to the M8-comparable 426 count. The scope includes the Session 68 host-driver source path as an identified input; it was not executed. Logs, receipts, backups, residual fixtures, caches, and external VM/evidence identities are not hashed by this source inventory and remain separate review gates.

| Comparison with M8 | Count |
|---|---:|
| M8 inputs | 424 |
| M13 candidate inputs | 426 |
| Added paths | 2 |
| Removed paths | 0 |
| Changed existing paths | 15 |

Added paths: `tests/arch_repository_security_test.cpp`, `tests/test_tmp_root.hpp`. The 15 changed paths are listed in the candidate JSON; they include CMake, Arch extraction, the verifier, prior M11/M12 test changes, and the M13 fixture-root changes. The manifest is a **local candidate only**. It does not update or replace M8, the official qualification inventory, or any security pin.

The final local Debug build produced these observed artifacts:

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `mpkg` | 17,224 | `26b39a1679de7f8807e692dd9f89b12df20d7f7c7c283c1d66711ac566723fc3` |
| `libathena_core.so` | 56,676,272 | `7498b349af1cd38c6aff4be0f2ba63a9b5838e35b3a42b3d73e7a15c93e9c3d2` |
| `arch_repository_security_test` | 1,140,232 | `d0d56485b0226278b8b00a2180f527d07c65695af1f89982cc840fe044b1a853` |

Observed toolchain: GCC 14.2.0, CMake 3.31.6, OpenSSL 3.5.7, LibArchive 3.7.4, Python 3.13.5. These versions are observations, not a lockfile. Artifact hashes identify bytes produced locally in this worktree; they do not prove source-to-binary causality, builder identity, reproducibility, custody, or authenticated provenance. The M7 local attestation is historical and was not treated as an attestation for M13. Historical/official pins remain unpromoted and cannot be assumed to match this candidate.

## Phase D — Alpha requirements and Debian VM readiness

| Requirement | M13 status |
|---|---|
| Debug build | PASS locally in isolated private build |
| Core nonprivileged suite | PARTIAL: 111 PASS, four TIMEOUT |
| Qualification source inventory | FAIL: 16 current inputs unlisted; unchanged official inventory |
| Arch archive path safety | PASS for local targeted adversarial suite |
| Common relative symlinks / prior-target hardlinks | PASS locally; corpus compatibility not established |
| Forward hardlinks / links to links | Unsupported; package rejected |
| Historical schema/recovery and power-loss guarantees | Still require independent qualification; local tests do not establish power-loss behavior |
| Approved source closure, candidate approval, pin review | BLOCKED / NOT DONE |
| Authenticated build provenance and independent evidence review | NOT AVAILABLE |
| Alpha M1 D6 fixture and evidence read-only reconciliation | NOT RUN in M13 |
| Approved Debian VM privileged campaign | NOT RUN by instruction; BLOCKED |
| Alpha M1 | **BLOCKED** |

### Gates before a Debian VM campaign

1. Reconcile the 16 omitted inputs and the M8/M13/official inventory scope through the reviewed inventory workflow; do not edit historical baselines in place.
2. Obtain review and approval of a frozen M13 closure, dependency/toolchain set, candidate identity, and any proposed pins. Build and provenance must be independently reviewed; current local hashes alone are insufficient.
3. Identify the approved Debian image/snapshot and boot identity, verify its provenance, capacity, filesystem/mounts, clock, and exact toolchain; rebuild from the approved closure.
4. Reconcile the exact privileged inventory against the approved runbook. Review actual commands and source behavior; keep scanner, collector, host driver, and installation operations outside the campaign unless separately authorized.
5. Read-only verify the Alpha M1 D6 residual fixture and all logs, receipts, reports, and backups; compare hashes/owner/mode/ACL and require no unexplained difference.
6. Complete independent security/evidence review and required administrative approval for the exact VM campaign. Preserve raw output, exit status, run identity, and all prior evidence.
7. Run only the approved cases in the approved Debian VM. Any FAIL, SKIP, TIMEOUT, NOT RUN, missing evidence, or mismatch is NO-GO. Review evidence independently before making a separate release decision.

No step of this Debian campaign was executed or scheduled by M13.

## Final result

M13 produced a locally built candidate and a separate, fully hash-checked source-closure proposal; added contained relative symlink and safe in-archive hardlink support; and ran the permitted nonprivileged CTest set. The unresolved qualification inventory failure and four test timeouts, unapproved source closure/pins, unauthenticated provenance, and unrun Debian VM campaign prevent release-candidate GO.

**NO-GO. Alpha M1 remains BLOCKED.** No privileged tests, collector, host driver, real installation, baseline/inventory/pin promotion, commit, or push occurred.
