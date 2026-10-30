# Monad 0.1 Alpha M14 — timeout root cause and source reconciliation

**Decision: NO-GO. Alpha M1 remains BLOCKED.** This report records local development and test evidence only. It does not qualify, approve, publish, or authorize an Alpha candidate.

## Starting state and preservation

The session started on branch `main`, `HEAD` `abd6bac4b368427b76f6c98522afde928b418939`. The worktree already contained extensive user-owned edits, reports, evidence, and build directories; those were preserved. The M13 report was read in full. No reset, clean, commit, push, installation, privileged test, scanner, collector, Session 68 host driver, official inventory/baseline/pin edit, or administrative operation was performed.

All M14 builds, temporary fixtures, and newly captured logs are under `/var/tmp/monad-alpha-tests/m14-20261028-103712-2`, a new owner-only `0700` directory. `TMPDIR` and `ATHENA_TEST_TMPDIR` pointed there for test runs. The starting filesystem had 90+ GiB available and over seven million free inodes; it remained above 90 GiB free during the run. Thus fixture exhaustion and storage pressure do not explain the M13 results. M13 evidence remains untouched under `/var/tmp/monad-alpha-tests/m13-20261028-BdVeKZ`.

## Phase A — timeout reproduction and correction

### M13 outcomes and conditions

M13 ran the final nonprivileged CTest batch serially from a Debug build, with the existing per-test CTest timeout of 600 seconds. The batch command was `ctest --output-on-failure -LE privileged -E '(scanner|collector|alpha_m1_|qualification_driver)'`. `TMPDIR` and `ATHENA_TEST_TMPDIR` were set to the private M13 directory. The retained output is `ctest-final-nonprivileged.log`; an earlier non-final exploratory log is separately retained as `ctest-nonprivileged.log`.

| Test | M13 batch result | M13 duration | Additional M13 evidence |
|---|---|---:|---|
| `activation_selection_test` | TIMEOUT | 600.10 s | Separate CTest diagnostic also timed out at 600.11 s |
| `activation_plan_persistence_boundaries_fresh_test` | TIMEOUT | 600.10 s | Printed inventory of 11,424 boundary points |
| `activation_plan_persistence_boundaries_bootstrap_test` | TIMEOUT | 600.10 s | Printed inventory of 11,417 boundary points |
| `activation_plan_persistence_boundaries_reused_test` | TIMEOUT | 600.10 s | Printed inventory of 10,511 boundary points |

M13 also completed the published persistence boundary test in 563.24 s. Its result shows this family was close to the configured limit even when it completed. M13's `activation_selection_test` diagnostic had reached the exhaustive I/O fault phase; this was progress evidence, not a passing result.

### M14 isolated reproductions

Each persistence mode was run alone from the fresh M14 Debug binary with the full interval and an explicit external 600-second limit. `timeout` returned 124 after 600 seconds in all three runs. The test printed its complete boundary inventory at the start and continued producing filesystem fixtures until termination.

| Test mode | Isolated command | Result | Inventory |
|---|---|---|---:|
| fresh | `timeout --signal=TERM --kill-after=5s 600s activation_plan_persistence_test boundaries-fresh` | TIMEOUT, 600 s, exit 124 | 11,424 |
| bootstrap | `timeout --signal=TERM --kill-after=5s 600s activation_plan_persistence_test boundaries-bootstrap` | TIMEOUT, 600 s, exit 124 | 11,417 |
| reused | `timeout --signal=TERM --kill-after=5s 600s activation_plan_persistence_test boundaries-reused` | TIMEOUT, 600 s, exit 124 | 10,511 |

The binaries were invoked by absolute path under the isolated M14 build; both test temporary-root variables remained set to the M14 private directory. Per-case start/end timestamps, output, and exit codes are retained as `repro-boundaries-{fresh,bootstrap,reused}.*`.

The original `activation_selection_test` was also reproduced alone with CTest: `ctest --test-dir /var/tmp/monad-alpha-tests/m14-20261028-103712-2/build --output-on-failure --timeout 600 -R '^activation_selection_test$'`. It timed out after 600.16 s. A separately bounded diagnostic run added phase timing and identified the exhaustive `stat` fault loop as the dominant cost:

| Selection phase | Result / duration |
|---|---:|
| Source-authority bootstrap | 0.19 s |
| Transitions | 0.56 s |
| Invalid-input cases | 1.72 s |
| `fsync` fault cases | 16 points, 3.12 s |
| `open` fault cases | 141 points, 26.38 s |
| `stat` fault cases | 5,529 points; at 600 s, 3,170 had completed |
| `pread` fault cases | 19.34 s when run independently |

The selection base (all checks except the isolated I/O fault loops) passed in 7.11 s. Filesystem state progressed through fixture creation and boundary checks. There was no observed lock wait, stuck child, invalid fixture, space/inode shortage, or sandbox denial. The cause is test duration: the full selection test combines 5,529 slow `stat` fault scenarios with the other complete fault matrices, while the persistence tests each execute over ten thousand independent fixture/fault/recovery scenarios in one CTest invocation. The tests were not deadlocked; each reproduction made continuing progress and was stopped by its explicit limit.

### Test-preserving correction

No production behavior, assertion, fault point, or configured timeout was weakened or raised.

- `activation_selection_test` now reports phase durations. Its three I/O fault kinds are separate CTest cases. The 5,529-point `stat` matrix is split into eight deterministic, contiguous ranges calculated from the measured inventory at runtime. Each case still checks every selected fault through the original failure, recovery, and state assertions. The base test timeout is restored to 180 seconds; each slow I/O shard retains a 600-second maximum.
- The existing `boundaries(path, first, last)` implementation already supports inclusive ranges. Each of `fresh`, `bootstrap`, and `reused` is registered as four 3,000-point ranges: `1..3000`, `3001..6000`, `6001..9000`, and `9001..12000`, clamped to the actual inventory. The 12 ranges are disjoint and cover every original boundary exactly once. Each retains the original 600-second CTest limit and all residue, errno, cause, outcome, and protected-state assertions. The published case was not changed.
- Progress diagnostics for the selection matrix report the inventory size, shard interval, and checked-point count. No timeout was increased to make a result pass.

The complete corrected runs passed:

| Corrected exhaustive group | Result | Duration |
|---|---:|---:|
| Selection `stat` shards, all 5,529 points | 8 PASS, 0 FAIL, 0 SKIP, 0 TIMEOUT | 1,063.48 s |
| Persistence `fresh`, 11,424 points | 4 PASS | 758.45 s cumulative |
| Persistence `bootstrap`, 11,417 points | 4 PASS | 743.67 s cumulative |
| Persistence `reused`, 10,511 points | 4 PASS | 623.03 s cumulative |
| All persistence shards | 12 PASS, 0 FAIL, 0 SKIP, 0 TIMEOUT | 2,125.16 s |

Selection `base`, `open`, and `pread` also passed in 7.11 s, 26.38 s, and 19.34 s, respectively. The cumulative persistence times are greater than the former 600-second limit, which confirms that the change partitions the complete work instead of masking it with a larger timeout.

## Phase B — 16-input source reconciliation

M13's qualification driver reported 16 present but unlisted source/test inputs. M14 reran `qualification_driver_test` in isolation; it remains **FAIL** because all seven Python subtests fail in `setUp` before their intended assertions. The exact error is `missing=[]; unlisted=[...]` with the 16 paths below. The official inventory was not edited.

The inventory test is driven by `tests/qualify_bounded_toml.py`, which requires an exact inventory of present regular files under `src`, `include`, `tests`, `cmake`, `third_party`, `packaging`, and `packages`, plus named root files. The 16 entries are:

| Input | Role | Bytes | SHA-256 |
|---|---|---:|---|
| `src/process/run.cpp` | Process invocation implementation | 1,453 | `169244d601e2322792cb990ad398d005031d209ca5830e9987e46058b8a60647` |
| `src/process/run.hpp` | Process invocation interface | 477 | `978410e1bbfb64425d2e62840a2416fb814227b1d7fec8a8b0f4f4f3d8dd3d3e` |
| `tests/alpha_m1_collector_v2_test.py` | Alpha M1 tooling test | 46,766 | `d64a0812e88a156ef1a63f16f840498e4a49280d952d9d8febe2fdf698734451` |
| `tests/alpha_m1_collector_v2_trust_test.py` | Alpha M1 tooling trust test | 18,086 | `63351fe66bb83eadfb0812a1f2edffcf081355c8820972f182f9f36a12b00881` |
| `tests/alpha_m1_ctest_inventory_test.py` | Alpha M1 inventory tooling test | 13,282 | `b55ac0949939811c26bbd6893a83ed7c8fb612d3ecb7c5a282415e29749865d3` |
| `tests/alpha_m1_evidence_tree_digest_test.py` | Alpha M1 evidence digest tooling test | 4,829 | `f11c9631632ee59773aea0bb432bf689ea5013ea7afa644edca41e507612d76c` |
| `tests/alpha_m1_installer_transaction_test.py` | Alpha M1 installer transaction test | 17,122 | `8c00ec74ae51be70eec1c07bad3357c46e1908c39defee117f73def7a00b37da` |
| `tests/alpha_m1_receipt_path_test.py` | Alpha M1 receipt-path test | 3,313 | `460d70dd4ba1b19b22bccd28dc55b96e21afb1ff66fb83ec50c82b0a014191e3` |
| `tests/alpha_m1_verified_evidence_runner_test.py` | Alpha M1 evidence runner test | 16,888 | `e0366da84a01c10dfa4ce856b9fe340a1212c423e7caf5c5e3a19152a95b2994` |
| `tests/alpha_m1_verified_execution_bootstrap_test.py` | Alpha M1 execution bootstrap test | 14,276 | `bd7c7ce361c2621940754f42900dd13b846e30afeb88ac0c8279918e81f9f699` |
| `tests/alpha_m1_vm_evidence_collector_test.py` | Alpha M1 VM evidence tooling test | 29,587 | `fc511df7cdefb17378faa792b091a0dc3750c5c138f5b52099785a766734969c` |
| `tests/arch_repository_security_test.cpp` | Arch archive security test | 10,892 | `d8476b3c263de683f361ef15a254b0e89331435257867b5b782a5e7ce89c1615` |
| `tests/archive_security_test.cpp` | Archive security test | 3,856 | `fdd2e373f79ea723e9db596d074a9e9ac300a21623db7b5e3e5af9d991617174` |
| `tests/monad_build_attest_test.py` | Local build-attestation tooling test | 3,305 | `d772828940682c9928f982659383ad1b2ad2bda0515e518ac6b774a5c2268ee6` |
| `tests/process_invocation_test.cpp` | Process invocation test | 2,739 | `dc163ed2d941d23473664e90e118cfe1ab7cf2b59821e745722d9c068f8fb65e` |
| `tests/test_tmp_root.hpp` | Shared private test-fixture root helper | 924 | `614a124e5fbb8383172f0479e94c5f5113644e26a8360fe3b6695b3aa14e4603` |

Classification: 2 implementation inputs, 3 C++ test/support inputs, 10 Python test/tooling inputs, and 1 shared test-fixture header. There are no generated build artifacts in these 16 entries.

The corresponding machine-readable local evidence is `/var/tmp/monad-alpha-tests/m14-20261028-103712-2/source-reconciliation-16.tsv`, SHA-256 `133590189546ce7ff1365c54892a3bd00023afd1412a275e45e743c2468e1f52`.

### M13 candidate comparison and proposal

M13's local candidate closure is `docs/development/monad-0.1-alpha-m13-source-closure-candidate-20261028.json`, SHA-256 `962b9148e2c15d4762835aef21a7dda85accf9c8999c54dbe1f32395bc264b40`. It contains 426 technical inputs, 49 separate qualification/process source inputs, and 14 named context documents. M14 recomputed all paths, sizes, and hashes: **zero mismatches** in all three sets. All 16 unlisted paths are already in the candidate technical closure, with matching hashes and sizes. No new or missing regular-file path was found under the candidate's technical roots. Additional qualification records and logs are generated/non-source evidence and fall under M13's explicit evidence exclusions.

At M14 start, before M14 edits, all 426 technical entries, 49 qualification/process entries, and 14 context entries matched M13; no subsequent-to-M13 source change was present in the worktree. The tracked-diff digest then was `5f16946800ee6f0b23122975c580b842066a54cd5c9deaf4cdf8ddc52ccf4277`, equal to the M13 candidate's recorded baseline. This is a local consistency check only.

M14 subsequently changed two technical inputs to implement and diagnose the timeout partition. Their final local hashes differ from M13 as expected:

| Path | M13 candidate SHA-256 | M14 local SHA-256 |
|---|---|---|
| `CMakeLists.txt` | `965a889246986a1b3056f515dcb96f969aaf252c82cca22c61f2c0a552984a3b` | `5ad1008c08a9d448ead167a3752669d391909d2de27c41d568c174bd503075b4` |
| `tests/activation_selection_test.cpp` | `511aee006df01c64b64316a39c884b2f5664d6df69d302fa17ad11c9876c8c6e` | `519a2f66b31cfe03925ed55170411e4efff360ed292e0e79fe68c2b768a3130b` |

Consequently, the M13 candidate is no longer a current closure for the M14 worktree. M14 did not generate or promote a replacement closure, and no installer/security pin was silently changed. A successor closure requires separate review and promotion.

**Reconciliation proposal, not applied:** prepare a reviewed successor to the local `tests/bounded_toml_sources.txt` exact-input list containing the 16 sorted paths above, then rerun its source-closure tests and review the intended scope. Keep this separate from any official Alpha inventory or pin promotion. Do not rewrite M8/M13 baselines or silently regenerate security pins.

Local hash equality does not authenticate authorship, origin, review, custody, or source-to-binary provenance. The M13 candidate is a locally stored proposal; its digest is not a signature or independent trust anchor. Authenticated provenance remains unavailable.

## Phase C — build and regression outcomes

| Check | Result | Evidence |
|---|---|---|
| Fresh isolated Debug configure | PASS | `final-configure.log` |
| Full Debug build | PASS | `final-build.log`, `cmake --build ... --parallel 4` |
| Targeted Arch/archive/process tests | PASS 3/3 | `targeted-arch-process-final.log` |
| Selection base/open/pread | PASS 3/3 | Individual CTest logs in the private M14 directory |
| Selection `stat` exhaustive shards | PASS 8/8 | `activation-selection-stat-shards-final.log` |
| Persistence fresh/bootstrap/reused shards | PASS 12/12 | `persistence-boundary-shards-final.log` |
| Qualification source inventory test | FAIL | `qualification-driver-final.log`; 7 errors before assertions because of the 16 unlisted paths |
| Full nonprivileged suite | PASS 134/134, 0 FAIL, 0 SKIP, 0 TIMEOUT | `final-nonprivileged-suite.log`; 4,017.37 s |

The final suite command is `ctest --test-dir /var/tmp/monad-alpha-tests/m14-20261028-103712-2/build --output-on-failure --timeout 600 -j 1 -LE privileged -E '(scanner|collector|alpha_m1_|qualification_driver)'`. The inventory had 169 registered tests: 134 selected, 35 explicitly excluded.

The 25 privileged-labelled tests were: `installed_semantics_integration_test`, `cli_install_integration_test`, `install_transaction_integration_test`, `generation_activation_test`, `realization_executor_integration_test`, `removal_realization_integration_test`, `immutable_object_publication_test`, `activation_execution_privileged_sources_test`, `activation_execution_privileged_legacy_test`, `activation_execution_privileged_owner_test`, `activation_execution_privileged_bind_test`, `activation_execution_privileged_nested_test`, `activation_execution_privileged_ext4_test`, `activation_plan_retention_privileged_owner_test`, `activation_plan_retention_privileged_bind_test`, `activation_plan_retention_privileged_nested_test`, `activation_plan_retention_privileged_ext4_test`, `activation_plan_persistence_privileged_test`, `legacy_d3_scanner_test`, `activation_plan_persistence_privileged_owner_test`, `activation_plan_persistence_privileged_bind_test`, `activation_plan_persistence_privileged_nested_test`, `activation_plan_persistence_privileged_tmpfs_test`, `activation_plan_persistence_privileged_ext4_test`, and `initial_activation_migration_test`.

The five scanner-named exclusions were `object_store_scanner_test`, `artifact_identity_scanner_test`, `artifact_scanner_test`, `transaction_gc_scanner_test`, and `package_operation_scanner_test`. The four excluded static/mock unit tests were `alpha_m1_canary_child_test`, `alpha_m1_canary_timeout_test`, `alpha_m1_admin_static_test`, and `alpha_m1_orchestrator_test`; `qualification_driver_test` was run separately above. No collector CTest case was registered. No Session 68 host-driver test was run.

`package_operation_test` in the selected set uses its simulated scanner fixture, not the production scanner or a privileged host scan. Packaging tests use temporary fixture/build paths and do not install into the host system.

### Final result classification

| Scope | PASS | FAIL | SKIP | TIMEOUT | NOT RUN |
|---|---:|---:|---:|---:|---|
| Corrected final CTest batch | 134 | 0 | 0 | 0 | 0 within selected batch |
| Separate qualification inventory test | 0 | 1 | 0 | 0 | 7 Python subtests errored before their intended assertions |
| M14 isolated pre-correction reproductions | 0 | 0 | 0 | 4 | — |
| M14 corrected selection exhaustive group | 11 | 0 | 0 | 0 | — |
| M14 corrected persistence boundary group | 12 | 0 | 0 | 0 | — |
| Privileged/scanner/Alpha M1 static and mock exclusions | 0 | 0 | 0 | 0 | 35 explicitly excluded from the final CTest batch |
| Collector / Session 68 host driver / Debian VM campaign | 0 | 0 | 0 | 0 | NOT RUN by instruction |

## Phase D — Alpha requirements, remaining blockers, and Debian VM gates

| Requirement | M14 status |
|---|---|
| Isolated Debug build | PASS locally |
| M13 timeout diagnosis | COMPLETE: exhaustive fault-matrix runtime, no deadlock/fixture/sandbox cause found |
| Complete selection and persistence fault coverage | PASS locally across disjoint shards |
| Qualification input inventory | FAIL/BLOCKED: 16 present inputs remain unlisted; official inventory unchanged |
| Candidate source closure comparison | PASS as a local hash/size comparison; not authenticated |
| Arch archive/path security regressions | PASS for local targeted tests; broad signed live-package compatibility is not established |
| Power-loss and independent recovery qualification | BLOCKED; local tests do not prove power-loss behavior |
| Frozen approved source closure, candidate identity, pin review | BLOCKED / NOT DONE |
| Authenticated build provenance and independent evidence review | NOT AVAILABLE |
| Alpha M1 D6 residual fixture and retained evidence reconciliation | NOT RUN |
| Approved Debian VM campaign and required privileged cases | NOT RUN by instruction; BLOCKED |
| Alpha M1 | **BLOCKED** |

### Residual blockers by priority

1. **P0 — Source/inventory approval:** review the 16-input reconciliation, define and freeze the intended closure, and use the approved inventory/pin promotion workflow. The local M14 report does not promote it.
2. **P0 — Provenance and candidate approval:** obtain an independently reviewed, authenticated source/build provenance record and approve exact candidate/toolchain/dependency identities.
3. **P0 — Debian VM campaign:** no approved VM identity/snapshot, privileged execution, or evidence review occurred in M14. Alpha M1 cannot be qualified from local tests.
4. **P1 — Qualification driver failure:** its seven tests remain blocked at setup by the source inventory mismatch; do not report these as passes.
5. **P1 — Residual fixture/evidence custody:** D6 residual fixture, logs, receipts, reports, and backups still need read-only identity/hash/owner/mode/ACL reconciliation on the approved VM.
6. **P1 — Filesystem/power-loss guarantees:** independent qualification remains required; successful local fault-injection tests are not proof of power-loss behavior.
7. **P2 — Arch ecosystem breadth:** signed live-package corpus compatibility, live mirrors, OpenPGP verification, and host installation were not tested.

### Separate authorization gates before any Debian VM campaign

1. Review and approve a frozen source closure, exact build inputs, toolchain/dependency set, candidate identity, and any proposed security-sensitive pins. Reconcile inventory through the reviewed promotion workflow; do not edit historical baselines in place.
2. Independently verify build provenance and approve the Debian image/snapshot, boot identity, filesystem/mount state, clock, capacity, and exact toolchain.
3. Read-only reconcile the D6 residual fixture and all retained logs, receipts, reports, and backups, including hashes and owner/mode/ACL metadata; preserve every item.
4. Review the exact privileged test inventory, commands, runner, pinned root-owned artifacts, canary receipt, prerequisites, and recovery procedure. Any administrative installation, rollback, privilege change, scanner, collector, or host-driver operation needs its own explicit approval and reviewed boundary.
5. Human approval is required for sudoers/gateway installation or changes, privilege expansion, security-control changes, state-changing rollback, and the exact VM campaign. Do not execute workspace binaries as root.
6. Run only approved cases in the approved disposable Debian VM. Preserve raw logs, exact commands, exit states, VM/run identity, hashes, fixture state, and independent review. Any FAIL, SKIP, TIMEOUT, NOT RUN, missing evidence, or unexplained mismatch leaves Alpha M1 BLOCKED.

## Final status

M14 resolves the four M13 timeouts by preserving their exhaustive fault coverage while bounding each CTest case. The separate qualification inventory failure, unapproved closure/pins, unauthenticated provenance, retained-evidence reconciliation, and unrun Debian VM campaign remain open. No GO is claimed.

`git diff --check` passed. No commit or push was made, and no official inventory, baseline, or security pin was modified.

**NO-GO. Alpha M1 remains BLOCKED.** No privileged test, real installation, Alpha M1 collector, Session 68 host driver, scanner, official inventory/baseline/pin change, commit, or push occurred.
