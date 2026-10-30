# Monad 0.1 Alpha M15 — release evidence and qualification readiness

**Decision: NO-GO. Alpha M1 remains BLOCKED.** This dossier consolidates local evidence and proposes a refreshed source closure. It does not qualify or authorize release, privileged execution, collector use, or installation.

## Scope and preservation

Initial state: branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`. The worktree contained extensive pre-existing modifications, untracked evidence, and build directories; these remain untouched. No reset, clean, deletion, commit, push, installation, privileged test, Alpha M1 collector, Session 68 host driver, or official inventory/baseline/pin edit was performed. M14 evidence under `/var/tmp/monad-alpha-tests/m14-20261028-103712-2` was read in place; its root is owner-only mode `0700`. No new fixture or build was created.

Reports M7, M8, M9, M12, M13, and M14 were reviewed. M7's local attestation and M8/M13 closure digests are local consistency records, not authentication. M9's security findings and historical test failures remain in the risk register below; M12's eight D4 tests and seven Arch/process/archive tests are historical targeted PASS results, not M15 execution. M13's four timeouts were superseded for local development by M14's test-preserving sharding, while its inventory failure remains.

## M14 evidence integrity and results

The following retained M14 logs and their SHA-256 values were recomputed locally:

| Evidence | SHA-256 | Reconciled result |
|---|---|---|
| `final-configure.log` | `f28c2b0c80e5b975215d35bbad7b4d733e16d9ce06785a17e2ec18281494cf17` | Configure PASS, report records isolated Debug |
| `final-build.log` | `b780d18246834d2a0eab52470ce7b71e6c0dad7a99cbb25557174cffbda80096` | Full local build PASS |
| `targeted-arch-process-final.log` | `7f487f24ac1f6e4c2090de386d73f4166b0918a485cebf0940efe5851621bcd6` | 3/3 PASS |
| `activation-selection-stat-shards-final.log` | `299d5f18f42ce40900e9378d82c861b1f3949a48e476bb439bf1304dedf99d62` | 8/8 PASS, 5,529 points, 1,063.48 s |
| `persistence-boundary-shards-final.log` | `d330855169596d5166a231361fc212da4b178b09ea4eb1327d084268bfca3283` | 12/12 PASS, 33,352 points, 2,125.16 s |
| `final-nonprivileged-suite.log` | `5631dc45b2a3fc81cf79a39d386ce3948f594a41e8f9b7cf8358ecf22b5152da` | 134/134 PASS, 0 FAIL/SKIP/TIMEOUT, 4,017.37 s |
| `ctest-inventory-final.json` | `629b7669325354cf0fefc73e3ab9436b7d10c0d19615326d52dc3d9b14bc13f4` | 169 registered; independently recounted 134 selected / 35 excluded |
| `qualification-driver-final.log` | `6d7afb4fb3bb5f1caa11501de5778c3442360e66e82453fd393cca77246cc246` | FAIL: seven subtests fail in setup on unlisted inputs |
| `source-reconciliation-16.tsv` | `133590189546ce7ff1365c54892a3bd00023afd1412a275e45e743c2468e1f52` | 16 paths; hashes and sizes match local files |

These values establish current byte consistency only. The evidence is colocated on the same host and is not signed or independently held; no independent custody/authenticity claim is made.

### Shard coverage and assertion retention

M14 changed test registration and partitioning, not production code, assertions, fault points, or timeouts. The current source computes selection shard endpoints from the measured fault inventory using inclusive ranges: `first=floor(N*(i-1)/8)+1`, `last=floor(N*i/8)`. With `N=5,529`, the eight intervals are `1..691`, `692..1382`, `1383..2073`, `2074..2764`, `2765..3455`, `3456..4146`, `4147..4837`, and `4838..5529`. They are contiguous, disjoint, and cover every point once. CTest's retained log shows all eight passing.

Persistence uses four inclusive nominal intervals per mode: `1..3000`, `3001..6000`, `6001..9000`, `9001..12000`, with the test clamping the last endpoint to the mode's enumerated inventory. Thus the effective final intervals are fresh `9001..11424`, bootstrap `9001..11417`, and reused `9001..10511`; the first three intervals are as listed. The 12 passing CTest cases cover 11,424 + 11,417 + 10,511 = 33,352 points. The underlying loop retains its per-point outcome, residue, errno/cause, recovery, and protected-state checks; no assertion was removed. M14 first recorded isolated TIMEOUTs for selection (`600.16 s`) and the fresh/bootstrap/reused persistence modes (each externally stopped at 600 s, exit 124); these are historical diagnostic results and were superseded by the passing partitioned runs. The coverage reconciliation is verified from current registration/loop source, retained PASS logs, and the M14 report, not by rerunning the long matrix.

### Final suite configuration and exclusions

The retained CTest inventory reports 169 registered tests. The final command recorded by M14 was:

```sh
ctest --test-dir /var/tmp/monad-alpha-tests/m14-20261028-103712-2/build \
  --output-on-failure --timeout 600 -j 1 -LE privileged \
  -E '(scanner|collector|alpha_m1_|qualification_driver)'
```

It selected 134, all PASS, with no selected-case SKIP, FAIL, or TIMEOUT. Thirty-five were deliberately excluded before invocation: 25 privileged-labelled cases, five unlabelled scanner cases, four static/mock Alpha M1 tests (`alpha_m1_canary_child_test`, `alpha_m1_canary_timeout_test`, `alpha_m1_admin_static_test`, `alpha_m1_orchestrator_test`), and `qualification_driver_test`. Recounting the retained inventory against the exact `-L`/`-E` filters gives 134 selected and 35 excluded. Privileged exclusions were `installed_semantics_integration_test`, `cli_install_integration_test`, `install_transaction_integration_test`, `generation_activation_test`, `realization_executor_integration_test`, `removal_realization_integration_test`, `immutable_object_publication_test`, `activation_execution_privileged_sources_test`, `activation_execution_privileged_legacy_test`, `activation_execution_privileged_owner_test`, `activation_execution_privileged_bind_test`, `activation_execution_privileged_nested_test`, `activation_execution_privileged_ext4_test`, `activation_plan_retention_privileged_owner_test`, `activation_plan_retention_privileged_bind_test`, `activation_plan_retention_privileged_nested_test`, `activation_plan_retention_privileged_ext4_test`, `activation_plan_persistence_privileged_test`, `activation_plan_persistence_privileged_owner_test`, `activation_plan_persistence_privileged_bind_test`, `activation_plan_persistence_privileged_nested_test`, `activation_plan_persistence_privileged_tmpfs_test`, `activation_plan_persistence_privileged_ext4_test`, `legacy_d3_scanner_test`, and `initial_activation_migration_test`. The five unlabelled scanner exclusions were `object_store_scanner_test`, `artifact_identity_scanner_test`, `artifact_scanner_test`, `transaction_gc_scanner_test`, and `package_operation_scanner_test`. The qualification driver was run separately and FAILED; its seven Python subtests did not reach their assertions. No collector CTest was registered. No Session 68 host-driver case ran. Packaging tests used temporary fixtures and did not install to the host. Results are therefore accurately stated as **134 PASS / 35 NOT RUN in the batch**, plus a separate qualification-driver FAIL; the result is not a full qualification pass.

## M8, M13, and M15 candidate source closure

A refreshed, machine-readable local candidate is [`monad-0.1-alpha-m15-source-closure-candidate-20261028.json`](monad-0.1-alpha-m15-source-closure-candidate-20261028.json). Its SHA-256 is `1864c42a6ae7871015f45c4058d7df9bd23040d59dfd475a3be5cd86f18a5be6`. It contains path, class, size, and SHA-256 for 426 technical inputs, 49 qualification/process source inputs, and 16 named review-context documents. The qualification/process scope remains 49 source-code inputs as in M13; generated reports, logs, receipts, backups, fixtures, JSON records, and caches are excluded and are not treated as source inputs. The two M14 source changes are reflected: `CMakeLists.txt` and `tests/activation_selection_test.cpp`. No technical path was added or removed relative to M13. The candidate was generated from current regular files in the M13-declared roots, excluding symlinks, `__pycache__`, generated build/evidence material, and this M15 dossier/manifest.

| Comparison | M8 | M13 | M15 candidate |
|---|---:|---:|---:|
| Technical input count | 424 | 426 | 426 |
| Qualification/process source inputs (separate scope) | Not comparable | 49 | 140 |
| M8 technical paths changed in M15 | — | — | 16 shared paths |
| Added relative to M8 | — | 2 | 2 (`tests/arch_repository_security_test.cpp`, `tests/test_tmp_root.hpp`) |
| Removed relative to M8 | — | 0 | 0 |

M15 has 16 changed paths versus M8 because it carries the 15 M13 changes plus M14's additional `tests/activation_selection_test.cpp` change; `CMakeLists.txt` is already among the shared changed paths. The M15 manifest also lists exact M8/M13 technical path and digest reconciliation and M13/M15 qualification-source reconciliation. M8, M13, and M15 are different dated proposals, not interchangeable baselines. This M15 candidate has not been approved, frozen, or promoted. Its tracked-diff hash and file hashes are local observations, not identity or provenance proofs.

### The 16 inputs absent from the official qualification inventory

M14's TSV matches all listed sizes and SHA-256 values against the local files. These are all present in M13/M15's technical scope; that does not repair the official inventory or make the qualification driver pass.

| Path | Classification | Bytes | SHA-256 |
|---|---|---:|---|
| `src/process/run.cpp` | implementation | 1,453 | `169244d601e2322792cb990ad398d005031d209ca5830e9987e46058b8a60647` |
| `src/process/run.hpp` | implementation | 477 | `978410e1bbfb64425d2e62840a2416fb814227b1d7fec8a8b0f4f4f3d8dd3d3e` |
| `tests/alpha_m1_collector_v2_test.py` | qualification tooling test | 46,766 | `d64a0812e88a156ef1a63f16f840498e4a49280d952d9d8febe2fdf698734451` |
| `tests/alpha_m1_collector_v2_trust_test.py` | qualification tooling test | 18,086 | `63351fe66bb83eadfb0812a1f2edffcf081355c8820972f182f9f36a12b00881` |
| `tests/alpha_m1_ctest_inventory_test.py` | qualification tooling test | 13,282 | `b55ac0949939811c26bbd6893a83ed7c8fb612d3ecb7c5a282415e29749865d3` |
| `tests/alpha_m1_evidence_tree_digest_test.py` | qualification tooling test | 4,829 | `f11c9631632ee59773aea0bb432bf689ea5013ea7afa644edca41e507612d76c` |
| `tests/alpha_m1_installer_transaction_test.py` | qualification tooling test | 17,122 | `8c00ec74ae51be70eec1c07bad3357c46e1908c39defee117f73def7a00b37da` |
| `tests/alpha_m1_receipt_path_test.py` | qualification tooling test | 3,313 | `460d70dd4ba1b19b22bccd28dc55b96e21afb1ff66fb83ec50c82b0a014191e3` |
| `tests/alpha_m1_verified_evidence_runner_test.py` | qualification tooling test | 16,888 | `e0366da84a01c10dfa4ce856b9fe340a1212c423e7caf5c5e3a19152a95b2994` |
| `tests/alpha_m1_verified_execution_bootstrap_test.py` | qualification tooling test | 14,276 | `bd7c7ce361c2621940754f42900dd13b846e30afeb88ac0c8279918e81f9f699` |
| `tests/alpha_m1_vm_evidence_collector_test.py` | qualification tooling test | 29,587 | `fc511df7cdefb17378faa792b091a0dc3750c5c138f5b52099785a766734969c` |
| `tests/arch_repository_security_test.cpp` | C++ regression test | 10,892 | `d8476b3c263de683f361ef15a254b0e89331435257867b5b782a5e7ce89c1615` |
| `tests/archive_security_test.cpp` | C++ regression test | 3,856 | `fdd2e373f79ea723e9db596d074a9e9ac300a21623db7b5e3e5af9d991617174` |
| `tests/monad_build_attest_test.py` | tooling test | 3,305 | `d772828940682c9928f982659383ad1b2ad2bda0515e518ac6b774a5c2268ee6` |
| `tests/process_invocation_test.cpp` | C++ regression test | 2,739 | `dc163ed2d941d23473664e90e118cfe1ab7cf2b59821e745722d9c068f8fb65e` |
| `tests/test_tmp_root.hpp` | shared test fixture support | 924 | `614a124e5fbb8383172f0479e94c5f5113644e26a8360fe3b6695b3aa14e4603` |

Classification totals: 2 implementation files, 10 Python tooling tests, 3 C++ regression tests, and 1 shared test helper. Required next action is owner review and an explicitly approved inventory promotion; no official inventory was edited here.

## Provenance and seven binary pins

M7's local attestation tied local file/config/log/artifact hashes together but was neither signed nor independently held and did not prove source-to-binary causality. M8 and M13 manifests likewise establish local hash/size consistency only. M14 local build artifacts diverge from all seven historical installer pins; M7 and M9 also reported divergence. M14 hashes below are read-only SHA-256 observations from its build directory, not replacement pins.

| Pinned artifact | Historical pin | M14 local hash | Missing evidence to reconcile |
|---|---|---|---|
| `activation_execution_privileged_test` | `d87a84cbb1e18ba073beaadb1bb7737b61fa448081af37e4d4c5a6c29337d157` | `4be7d8265e51b28f92c363fbd64938610d54eaf73cdcb02d71f0132a73c3677e` | Exact pinned binary/build record, frozen source/toolchain, signing/custody chain, reason for changed bytes, reviewed promotion |
| `activation_plan_retention_test` | `f76251d6e0af0bd892d1dcb17f41f80b13343c7c274e289014dfcb090370e474` | `f1d4292313b56cc31db75b1df6a92925c68b90a48c173d01d2027883e26424e1` | Same evidence; include its exact source and CMake configuration |
| `activation_plan_persistence_test` | `f12620d89bb55e66f1e1960dd8852f2fdf27c9a148967d222d562fdddbd5aaa3` | `01c68b15a753f43e20f4fb10ed44f0fa24208c107398db7da6e0bcf5bb3bde00` | Same evidence; reconcile M14 test partition/source and historical build input |
| `legacy_d3_test` | `61e23e8c9cd3a81c8766eb61e93e01cc7c9af2025a80f6056f28d3ac137ad6a9` | `ef41350d7b33d7d2af2406c4609ef28052ea8860fa226787548d0dbae9b86840` | Exact pinned binary/build record, frozen source/toolchain, signing/custody, reviewed promotion |
| `initial_activation_test` | `934222dc181ae1a98a3d00bed5cba8e208cdad194d0c10fe552b1e5cf7e74243` | `a898b8d783fcbb0f744020af16a2dc68e761af0c5434ec186b63ae00624430c8` | Same evidence; include exact source and linked library inputs |
| `libathena_core.so` | `e212420cb34aeca76cebe2ecf396832f25c3c4f119b846b42ef00d2994cc995a` | `1696d161896ffcfbf8d77863e819c42c4da25086cbfbb398bae04f99cf7479f0` | Exact source closure, compiler/linker/sysroot/dependencies and build path; authenticated build record and custody |
| `CTestTestfile.cmake` | `99751015487882864d67b948dbbb0eee4ac7204aca1b7ee20435f8e69b656bd5` | `a1afa7120bbea849eebc352c19dae7b99592fd695d2fb27e5b00c524b5b8090c` | Exact CMake version/options, source/build paths, generated inventory, frozen inputs and signed custody |

All seven are **UNRECONCILED / NOT PROMOTED**. M9 noted that build paths differ and can affect embedded metadata; this is a plausible factor, not a complete byte-level explanation. Reconciliation requires the original pinned artifacts or authenticated build records, exact source and configuration, toolchain/dependency identities, independent reproducible rebuild/comparison, and authorized review. No local hash has been substituted into any pin.

## Debian VM qualification campaign checklist — NOT EXECUTED

The campaign is a future gated operation. Each phase must retain exact command, stdout/stderr, exit status, timestamps, VM/run identity, input hashes, and reviewer disposition. Preserve the M1 D6 residual fixture and every existing log, receipt, report, and backup; compare read-only and stop on any unexplained difference.

| Phase | Preconditions / authorization | Proposed checks or command shape | Expected result | Stop criteria |
|---|---|---|---|---|
| 1. Nonprivileged preparation | Owner-approved frozen candidate closure and official inventory reconciliation; independently approved Debian image/snapshot; isolated owner-only `0700` workspace under `/var/tmp/monad-alpha-tests`; adequate space/inodes; no existing evidence overwritten | Read-only `id`, `uname -a`, `findmnt`, `df -h`, `df -i`, `cmake --version`, `c++ --version`; verify transferred manifest with `sha256sum -c`; configure/build from the approved source snapshot in a new private build path; compare CTest inventory and exact selected set to the approved manifest | VM identity, mount/capacity/toolchain recorded; hashes match; clean configure/build; exact inventory reconciled; required nonprivileged tests all PASS | Unknown image/snapshot, hash mismatch, low capacity, unexpected mount/toolchain/inventory, missing/changed D6/evidence, any required FAIL/SKIP/TIMEOUT/NOT RUN |
| 2. Nonprivileged functional gate | Phase 1 reviewed; approved test list and environment; sufficient private `TMPDIR` and `ATHENA_TEST_TMPDIR` | `ctest --test-dir "$BUILD" --output-on-failure --timeout 600 -j 1 -R '<reviewed exact nonprivileged set>'` | Every required case PASS; preserve output and status separately | Any unreviewed test is selected, any required case not executed or not PASS, evidence discrepancy |
| 3. Privileged VM checks | Separate human approval for exact VM, snapshot, root-owned hash-pinned artifacts, exact commands/arguments, allowlist, capabilities, canary receipt, recovery plan, and reviewed privileged inventory. Never execute workspace binaries as root; invoke only the reviewed gateway | **No command authorized or run in M15.** After approval, use only the exact reviewed gateway invocation and exact approved case IDs; do not use a broad `ctest -L privileged` selector without inventory review | All required privileged cases PASS with capability/environment validated and complete evidence | Missing approval/artifact pin/capability/canary, any mismatch, FAIL/SKIP/TIMEOUT/NOT RUN, unexpected host state, evidence loss |
| 4. Administrative installation / rollback | Separate explicit human approval after prior gates; root-owned reviewed artifact with exact hash/argument allowlist; approved disposable VM; backup and recovery plan; D6 fixture preserved | **No install/rollback command authorized or run in M15.** Use only the separately approved administrative procedure; no development shell or workspace executable as root | Installation state and receipts match approved plan; independent evidence review | Any deviation, privilege expansion, changed administrative path, failed health check, rollback ambiguity, D6/evidence mutation |
| 5. Evidence review and release decision | Independent reviewer with access to immutable raw records; all prior phases closed | Verify hashes, identities, permissions/ACLs, case inventory/statuses, receipts, backups, and discrepancy register; record reviewer sign-off tied to exact candidate digest | Complete evidence with no unresolved required result; independent approval | Missing or unauthenticated required evidence, unresolved discrepancy, any required case not PASS |

This checklist is not an authorization. Scanner/collector and Session 68 host-driver operations remain out of scope and require separate review and explicit authorization; none was invoked.

## Residual security blockers by priority

| Priority | Blocker | Required closure |
|---|---|---|
| P0 | Official inventory omits 16 present inputs; qualification driver still FAILs | Review scope and promote a versioned inventory through the approved workflow; rerun its driver after approval |
| P0 | No approved frozen closure/candidate and no authenticated source-to-build provenance | Independent source/build review, locked toolchain/dependencies, authenticated builder/custody, exact candidate approval |
| P0 | Seven protected binary pins diverge from local artifacts | Reconcile original artifacts/build evidence; independent reproducibility and explicit reviewed pin promotion |
| P0 | Approved Debian VM campaign, required privileged cases, physical scanner proof, and independent evidence review have not happened | Complete only after all preconditions and explicit authorization; mandatory privileged and scanner cases must pass |
| P1 | M1 D6 fixture and retained evidence have not had read-only hash/owner/mode/ACL reconciliation in this work | Reconcile on approved VM without mutation and preserve all prior evidence |
| P1 | Local fault tests do not prove power-loss durability or cross-namespace recovery | Independent reboot/power-loss and recovery qualification under reviewed conditions |
| P1 | M9 identified high-risk SHA-256 shell invocation and Arch extraction issues; M12/M13 document subsequent fixes and local hostile tests | Independent security review of final M15 source and adversarial qualification; no claim from historical tests alone |
| P1 | Legacy metadata strictness and package build scripts' user authority remain concerns | Define strict schema/trust policy and evaluate build isolation before qualification |
| P2 | Arch compatibility across signed live package corpus is unproven | Separate approved corpus/signature compatibility review; no live network/install in M15 |

## Decision

**NO-GO. Alpha M1 remains BLOCKED.** The M14 evidence supports a local 134/134 filtered suite and complete partitioned fault matrices, while separately recording the qualification-driver FAIL and 35 excluded CTest cases. The refreshed M15 closure is a local candidate only. Official inventory reconciliation, provenance, all seven pins, D6/evidence custody, security review, and the approved Debian VM campaign remain open. No installation, privileged test, collector, Session 68 host driver, official baseline/inventory/pin change, commit, or push occurred.
