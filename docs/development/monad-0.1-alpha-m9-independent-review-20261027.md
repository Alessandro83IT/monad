# Monad 0.1 Alpha M9 — independent review and qualification

**Decision: NO-GO. Alpha M1 remains BLOCKED.** This is an independent technical review within one agent/session, not an organizationally independent approval. No privileged test, scanner campaign, Alpha M1 collector, Session 68 host driver, real installation, pin promotion, source modification, baseline change, commit, or push was performed.

## Scope and evidence classes

The review began at branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`. The M8 manifest records 124 pre-existing Git status entries. All 124 remain present with unchanged status in M9; the only additional default-status entries are the two M9-owned paths `.m9-independent-build/` and `.m9-tmp/`. Existing build directories, evidence and worktree changes were preserved. The `git diff` SHA-256 remains `782d43b2ec90419a40d41db5e506fb0d3d39742828640155d47d672c232eb58c`, matching the M8 baseline record.

Evidence is separated as follows:

- **Developer-reported historical evidence:** M1–M8 reports and their referenced outputs. They were read; their results were not treated as M9 results.
- **Repeated in M9:** all 424 M8 closure entries; M8 manifest digest; M7 attestation internal verification; fresh CMake configure and complete Debug build; CTest discovery and one selected functional run; source-level security review; M7/M9 artifact hashes.
- **Not verifiable here:** authenticated source/build provenance, approved candidate identity, target Debian filesystem behavior, physical scanner behavior, actual privileged qualification and evidence custody.
- **Human decisions required:** approve/reconcile candidate source closure and official inventory; review security findings and any proposed pins; approve the VM campaign and operational/admin steps; conduct independent evidence review before any release decision.

A same-agent M9 review is not a second organizational reviewer. No claim of organizational independence is made.

## Documentary review: M1–M8

All eight reports were read in full. The historical findings reconcile as follows: M1/M2 report the official 414-entry qualification inventory failure and selected local builds; M3 records archive/store hardening; M4 distinguishes process-crash observation from power-loss durability; M5 retains the inventory failure; M6 documents inventory and pin divergence; M7 documents a local unauthenticated build attestation and seven differing binary pins; M8 proposes a 424-entry technical source closure and retains NO-GO. Historical PASS counts remain historical and were not added to M9 totals.

M7 attestation verification was repeated:

```text
python3 scripts/monad_build_attest.py verify --record docs/development/monad-0.1-alpha-m7-evidence-20261027/local-build-attestation.json
PASS: local record, inputs, cache, and artifacts are internally consistent; not authenticated
```

This confirms internal consistency only. It does not authenticate the record, builder, environment, approval, or source-to-binary causality.

## M8 source closure verification

Manifest: `docs/development/monad-0.1-alpha-m8-source-closure-20261027.json`
Recorded and independently recomputed SHA-256: `8ff98f55fafd85808228a97e878e44b9284325364e774bfaac8d1005b8a00d4c`.

| Check | M9 result |
|---|---|
| Declared entries | 424 |
| Unique paths | 424 |
| Missing/non-file inputs | 0 |
| SHA-256 or recorded-size mismatches | 0 |
| Symlink inputs | 0 |
| Eligible files under declared roots plus fixed files | 424 |
| Eligible unlisted files / manifest paths outside computed scope | 0 / 0 |
| Manifest digest | PASS, matches M8 |
| Tracked diff digest vs M8 | PASS, unchanged |
| M8 status records | 124/124 statuses unchanged |

The check covered file existence, type, size and SHA-256, duplicate paths, and the stated root/fixed-file scope. It did not promote or alter the closure. No subsequent content change was found among the 424 declared inputs. M9 build/evidence output is outside that source scope.

**Scope limitation:** this is a 424-file *technical source* closure, not a complete build-input closure or an approved qualification closure. The compiler, CMake, linker, system libraries, Python, environment variables, filesystem, build path, and package-manager state are not locked by the manifest. The manifest itself identifies declared dependencies but does not lock exact dependency bytes or authenticate them. Qualification/admin procedures and records under `docs/qualification`, `docs/security`, `qualification`, most of `scripts`, `tools`, and `.codex*` are outside technical scope and must be separately identified and reviewed for qualification. This limitation is disclosed in M8; M9 confirms it remains open.

## Independent Debug build

The first fresh configure/build in `/tmp/monad-m9-independent-build` configured successfully but the build stopped when `/tmp` ran out of space (`No space left on device`). It had compiled `athena_core` and part of the targets; it was not counted as a complete build. The existing `/tmp` contents were not cleaned or removed.

A fresh isolated configure and full build then completed successfully in the new M9-owned directory `.m9-independent-build/`, using `.m9-tmp/` for compiler temporaries. The configure/build logs and generated inventory remain there.

| Build property | M9 observation |
|---|---|
| Configure | PASS, Debug, fresh build directory |
| Compiler | `/usr/bin/c++`, GCC 14.2.0 (`Debian 14.2.0-19`) |
| CMake | 3.31.6 |
| C++ mode | C++20, from project configuration |
| CMake generator/build tool | Unix Makefiles / GNU Make |
| OpenSSL Crypto | 3.5.7 |
| LibArchive | 3.7.4 |
| Threads | found |
| Python | 3.13.5 |
| Build | PASS, all targets, `--parallel 4` |
| Build compile database | `.m9-independent-build/compile_commands.json`; SHA-256 `550b4204797f222eb69be843c08a379cf460394f15a8f4c627acca595398e302` |
| Artifact inventory | 95 main/test artifacts in `.m9-independent-build/artifact-hashes.sha256`; list SHA-256 `34cc043a9d46b736bb2c6b0b81dbec7acba2a57239f61279197682551c10c908` |

The first attempt's environmental failure and the successful retry are both retained in the M9 evidence directory/workspace. The successful build is of the exact current technical closure, but it is not an authenticated or approved release build.

### M7, historical pins, and M9 artifact comparison

The following M9 hashes were recomputed after the successful build. M7 hashes are from its local attestation/report; historical installer pins are from the M7/M8 tables. All seven M9 outputs differ from both corresponding M7 output hashes and the historical pin values.

| Artifact | Historical pin | M7 artifact | M9 artifact |
|---|---|---|---|
| `activation_execution_privileged_test` | `d87a84cbb1e18ba073beaadb1bb7737b61fa448081af37e4d4c5a6c29337d157` | `e9ddcbd05418892fae89af4a42b95b7d15674496be20660e4def6fd76f84a590` | `416300fea03b98df11f7ff2a9109dbb637bbf5ced61315e9a67c991b515b439b` |
| `activation_plan_retention_test` | `f76251d6e0af0bd892d1dcb17f41f80b13343c7c274e289014dfcb090370e474` | `125280b20a7c4dfe750ffe1190391c59bbaf704c3190ebf92e304efa8ff4520e` | `8eb5c5448a28f5047b11ce6ea5981dc055a406929391b56b5111bab4dda3d856` |
| `activation_plan_persistence_test` | `f12620d89bb55e66f1e1960dd8852f2fdf27c9a148967d222d562fdddbd5aaa3` | `500c6c586b52366bb887316630484a2baf5e91207698a60fe7e87f0df48f69d7` | `eb90acb9a5725ad90057e8c0d884c75f9f46c4014620b0da40d7177cd2477521` |
| `legacy_d3_test` | `61e23e8c9cd3a81c8766eb61e93e01cc7c9af2025a80f6056f28d3ac137ad6a9` | `bfa5cfa569a32302ae9287514f348fe04e6b64511981cb97c63df71d93887f8a` | `9f21ad7382859da496568344d6a62892607dc39d7e569521430cb3ba961236fc` |
| `initial_activation_test` | `934222dc181ae1a98a3d00bed5cba8e208cdad194d0c10fe552b1e5cf7e74243` | `000ca18058c62466df944c46cf79ccf6fcbe8820ddd38edf3e5d5da0f4962a7c` | `24274eae9cc2c90742d5756938e45321a69e141e162896b6340f0fcbc5dfec97` |
| `libathena_core.so` | `e212420cb34aeca76cebe2ecf396832f25c3c4f119b846b42ef00d2994cc995a` | `ba80321ba09e1cc57f5c4294b412914fec50abcb8ac7c05b101861182a4307d2` | `38a5cce5e3dd9f2b24e8c7f0b4b75fb3162bf193a36a31b82a010d36273c5a41` |
| `CTestTestfile.cmake` | `99751015487882864d67b948dbbb0eee4ac7204aca1b7ee20435f8e69b656bd5` | `fed72115ca1a16e620e6ed9e96cef72742901655931680eef79fcfd60029fd9d` | `2b4f37c48763f285e93bc4f7ba2aa410e2c274a26c85acb8210c4cb37e17d519` |

M7 used `/tmp/monad-m7-provenance-build`; M9 used `/home/alessandro/Projects/monad/.m9-independent-build`. The compiler/CMake/dependency versions observed are the same as M7, but the absolute build paths and environments are not identical. Absolute build paths can be embedded in debug/build metadata; `CTestTestfile.cmake` also embeds the configured build path. Thus the M9-vs-M7 hash differences are **explained as a non-like-for-like path/environment comparison**, but exact byte-level causes were not exhaustively isolated. They do not demonstrate reproducibility. All historical pins remain unchanged and are **not approved for this candidate**. No new pin is proposed by M9.

## Functional CTest results

CTest discovery found 149 registered tests. Selection was performed after inspecting CTest labels, test commands, and test source roles. The run selected 111 and excluded 38 before invocation:

- all `privileged`, `diagnostic`, and `packaging` labels;
- scanner-named tests even where they lack the `privileged` label;
- Alpha M1 canary/admin/orchestration tests;
- the qualification fixture-cleanup test.

Scanner exclusions included `object_store_scanner_test`, `artifact_identity_scanner_test`, `artifact_scanner_test`, `transaction_gc_scanner_test`, and `package_operation_scanner_test`; labeled scanner tests were excluded as well. The registered Alpha M1 tests were excluded. No collector, host driver, real install, scanner, or privileged command was invoked. The 25 CTest entries currently labeled privileged, plus the additional scanner tests without that label, were all excluded before execution. The inventory was generated at `.m9-independent-build/ctest-inventory.json`.

Command:

```sh
ctest --test-dir .m9-independent-build --output-on-failure -j4 \
  -LE 'privileged|diagnostic|packaging' \
  -E 'scanner|alpha_m1|activation_plan_persistence_qualification_cleanup_test' \
  --output-junit .m9-independent-build/ctest-results.xml
```

| Result | Count | Detail |
|---|---:|---|
| PASS | 102 | Selected nonprivileged fixture/unit/integration cases |
| FAIL | 9 | Eight fixture/capacity failures under full `/tmp`; one qualification inventory failure |
| SKIP | 0 | No selected test returned skip |
| TIMEOUT | 0 | No selected test timed out |
| NOT RUN | 38 | Excluded before execution as described above |

The eight environment-contaminated FAILs were `operational_request_runtime_test`, `activation_plan_persistence_test`, `transaction_gc_test`, `activation_plan_retention_limits_test`, `activation_plan_retention_budgets_test`, `activation_plan_persistence_bounds_test`, `activation_plan_persistence_publication-capacity_test`, and `activation_plan_persistence_recovery-capacity_test`. Their observed messages were `fixture write`, `write fixture`, or `No space left on device`; `/tmp` had 12 MiB available at observation and remained 100% full. They remain recorded as FAIL, not converted to PASS or SKIP. A clean rerun requires an independently approved/available test filesystem with adequate capacity; no cleanup was attempted.

`qualification_driver_test` failed in all seven setup cases because the official 414-path inventory omitted 14 current inputs: `src/process/run.cpp`, `src/process/run.hpp`, nine Alpha M1 tooling tests (`alpha_m1_collector_v2_test.py`, `alpha_m1_collector_v2_trust_test.py`, `alpha_m1_ctest_inventory_test.py`, `alpha_m1_evidence_tree_digest_test.py`, `alpha_m1_installer_transaction_test.py`, `alpha_m1_receipt_path_test.py`, `alpha_m1_verified_evidence_runner_test.py`, `alpha_m1_verified_execution_bootstrap_test.py`, `alpha_m1_vm_evidence_collector_test.py`), `tests/archive_security_test.cpp`, `tests/monad_build_attest_test.py`, and `tests/process_invocation_test.cpp`. No baseline was edited. This is consistent with the documented scope divergence, but remains a qualification blocker until an approved inventory is reconciled.

Full CTest output and JUnit are retained at `.m9-independent-build/ctest.log` and `.m9-independent-build/ctest-results.xml`.

## Security review: M1–M4 corrections and residual risks

### Corrections confirmed in source

- Package names are restricted to alphanumeric plus `._+-` with an alphanumeric first character; versions are parsed before package load/store path use.
- The common process runner uses fork/exec argv rather than a shell for the reviewed downloader and CMake/Autotools build paths.
- The LibArchive source extractor uses a private mode-0700 directory and rejects traversal, absolute names, links, special files, duplicate normalized paths, collisions, special permission bits, and configured entry/size limits.
- Legacy store staging rejects symlinks/special files, uses no-follow descriptor-relative reads, writes a private sibling stage, and publishes with `renameat2(RENAME_NOREPLACE)` so an existing destination is preserved.
- Recovery candidate inspection is read-only and does not infer ownership or delete ambiguous stages.
- Generation switching validates the selected generation before mutation and uses guarded publication/selection protocols; it does not make the full multi-namespace install/remove/recovery workflow one atomic transaction.

These are source observations, not privileged or adversarial qualification results. The targeted M3/M4 tests are historical; M9's CTest run had environment-dependent failures as listed above.

### New/remaining findings

1. **High — SHA-256 verification still invokes a shell with an unsafe quoting form.** `src/verify/verifier.cpp::verify_sha256()` constructs `sha256sum "<path>"` and calls `popen()`. A path containing shell command substitution, backticks, or quote syntax can escape double-quote protection and execute with the caller's authority. This path was not covered by the M1 direct-argv conversion and has no M9 dynamic test. Route through the direct-argv process primitive or a library hash implementation and add path-metacharacter regressions in a later implementation session.
2. **High — Arch package extraction is not link-safe.** `src/repository/arch_repository.cpp::extract_package()` lists members with external `tar`, checks only absolute and `..` names, then runs `tar --zstd -xf` directly into the destination. Safe names do not prevent a symlink/hardlink member followed by a write through that link; no private extraction root/type rejection or no-follow extraction is established here. This can escape staging depending on archive ordering and tar behavior. The review did not execute a malicious archive. Replace with validated LibArchive extraction or a separately proven link-safe extraction protocol and adversarial fixtures.
3. **Medium — Arch commands remain shell-based.** The POSIX single-quote helper protects shell metacharacters for ordinary arguments, but `curl` receives the metadata-derived URL without an end-of-options `--`; option-like values can be interpreted as curl options. Arch download/inspection/extraction still uses `system`/`popen`; review and move to argv execution with explicit option termination.
4. **Medium — legacy package metadata remains permissive.** Wrong-typed optional arrays/entries are skipped in `package_loader.cpp` rather than rejected. M1/M8 already record this schema strictness gap; M9 confirms it remains.
5. **Medium — crash/power-loss and cross-namespace recovery remain unqualified.** Legacy store publication is atomically visible but lacks fsync-based power-loss durability. Generation, current selection, projections, store and transaction pins span multiple namespaces; process-crash/fault tests do not prove reboot/power-cut behavior. Ambiguous stages remain manual/uncertain by design.
6. **Medium — package build scripts run with invoking-user authority.** The build backend invokes package-provided configure/CMake/make behavior without an execution sandbox. The supported trust model and isolation remain open.
7. **Low/Medium — build identity is not locked/authenticated.** The technical closure does not pin compiler, linker, system library bytes, environment, or builder identity. The M7 record is locally self-consistent only.

No source or test was changed to address these findings.

## Discrepancy register

| Comparison | Classification | Reason |
|---|---|---|
| M8 closure JSON digest vs recomputed digest | **SPIEGATA / PASS** | Exact digest match; all 424 entries exist and match size/hash. |
| M8 closure vs current technical roots | **SPIEGATA / PASS for declared scope** | The computed root/fixed-file set is exactly 424; no omitted eligible file or extra manifest path. |
| 414 official inventory vs current M8 424 closure and CTest result | **BLOCCANTE** | Counts describe different scopes; the official qualification driver fails on 14 unlisted current inputs. Scope differences explain the numbers but no approved reconciliation/promotion exists. |
| Historical M2 426 and M6 427 inventories vs M8 424 | **SPIEGATA as differing snapshots/scopes; BLOCCANTE for candidate approval** | M6 records one addition and six shared digest changes from M2; M7/M8 deliberately use a narrower 424-input technical scope. They are not interchangeable and no candidate closure has been approved. |
| Seven historical installer pins vs M7 artifacts | **BLOCCANTE** | All seven differ per M7/M8. No security-reviewed promotion occurred. |
| Seven historical pins/M7 artifacts vs M9 artifacts | **SPIEGATA as non-identical build path/environment; BLOCCANTE for qualification** | Tool versions match, but build directories differ and exact difference causes were not isolated. No reproducibility or candidate equivalence claim follows; pins stay unchanged. |
| M7 attestation internal verification | **SPIEGATA / limited PASS** | M9 reproduced its internal-consistency PASS; it is explicitly unauthenticated and not a provenance proof. |
| M9 CTest FAILs | **BLOCCANTE** | Eight are visibly contaminated by `/tmp` exhaustion; one is the official inventory mismatch. Functional gate is not clean. |
| M9 independent build | **SPIEGATA / PASS, local only** | Complete Debug build succeeded from the current closure with recorded local toolchain/dependencies; not approved/authenticated. |

No pin, baseline, inventory or closure was edited or promoted.

## Qualification readiness: exact work still required on the approved Debian VM

### Safe, nonprivileged prerequisites

1. Human owner/reviewer approves a specific candidate closure and resolves the 414/426/427/424 scope discrepancy without rewriting historical records.
2. Verify the approved VM image identity, snapshot, boot ID, kernel, filesystem/mount/options, available space, clock and toolchain/dependency set using read-only preflight.
3. Transfer only the approved closure/build artifacts; verify hashes and custody at source, transfer boundary and guest. Rebuild in a clean isolated guest build directory and archive configure/build logs, compile commands and artifact hashes.
4. Compare guest CTest inventory against the approved inventory and classify each test from command behavior, labels and source—not names alone. Ensure no unapproved scanner, collector or installation entry is selected.
5. Preserve and read-only verify Alpha M1 D6 residual fixtures and all existing logs, receipts, reports and backups; capture owner/mode/ACL and hashes. Any missing or changed item is NO-GO.
6. Re-run the allowed nonprivileged suite after ensuring the VM test filesystem has adequate capacity; retain PASS/FAIL/SKIP/TIMEOUT/NOT RUN separately. Review the M9 security findings and obtain a separate security review.
7. Obtain explicit approval of the exact test list, artifact hashes, VM snapshot and run procedure before any privileged or administrative operation.

### CAP_SYS_ADMIN and privileged checks — NOT RUN in M9

The current CTest inventory marks the following 25 cases privileged; their exact guest selection must be reconciled against the approved inventory before launch:

- `installed_semantics_integration_test`, `cli_install_integration_test`, `install_transaction_integration_test`, `generation_activation_test`, `realization_executor_integration_test`, `removal_realization_integration_test`, `immutable_object_publication_test`;
- `activation_execution_privileged_sources_test`, `activation_execution_privileged_legacy_test`, `activation_execution_privileged_owner_test`, `activation_execution_privileged_bind_test`, `activation_execution_privileged_nested_test`, `activation_execution_privileged_ext4_test`;
- `activation_plan_retention_privileged_owner_test`, `activation_plan_retention_privileged_bind_test`, `activation_plan_retention_privileged_nested_test`, `activation_plan_retention_privileged_ext4_test`;
- `activation_plan_persistence_privileged_test`, `activation_plan_persistence_privileged_owner_test`, `activation_plan_persistence_privileged_bind_test`, `activation_plan_persistence_privileged_nested_test`, `activation_plan_persistence_privileged_tmpfs_test`, `activation_plan_persistence_privileged_ext4_test`;
- `legacy_d3_scanner_test`, `initial_activation_migration_test`.

Also capability-sensitive scanner tests lacking a privileged label were excluded and require exact review before guest use: `object_store_scanner_test`, `artifact_identity_scanner_test`, `artifact_scanner_test`, `transaction_gc_scanner_test`, and `package_operation_scanner_test`. Their CTest label state is not a substitute for the actual CAP_SYS_ADMIN requirement.

Only an approved Debian VM campaign may execute the approved subset. Do not infer that every listed case is required until the controlling approved inventory/runbook is reconciled; do not attempt degraded execution without the required capability.

### Administrative steps and explicit approvals — NOT RUN

- Alpha M1 collector: not invoked; requires approved candidate, host and collector procedure, then evidence custody/review.
- Gateway/staging/sudoers/systemd canary or privilege changes: not performed; require separately reviewed root-owned hash-pinned artifact, exact argument allowlist, and human approval.
- Real installations, state-changing rollback/recovery, or changes to installed administrative paths: not performed; require explicit authorization after all prior gates.
- Session 68 host driver: not invoked.
- Final evidence review and release GO decision: requires a reviewer organizationally independent from this same-agent analysis, plus approvals recorded against the exact artifacts and receipts.

Any FAIL, SKIP, TIMEOUT, NOT RUN in a required case, evidence loss, hash mismatch, or unresolved discrepancy keeps Alpha M1 BLOCKED.

## Residual risk and decision

The closure's bytes are internally verified, and a complete local Debug build succeeded. However, the official qualification driver fails, eight selected tests failed under a demonstrably full `/tmp`, M9 and historical artifact hashes do not match, no candidate/pin approval exists, the M7 attestation is unauthenticated, security risks remain in shell-based hashing and Arch extraction, and the approved Debian VM campaign/evidence review has not occurred.

**Technical decision: NO-GO. Alpha M1 remains BLOCKED.** This review does not authorize pin promotion, privileged testing, installation, or release.

## Concrete M10 plan

1. Preserve this M9 report and all M9 build/test evidence; do not alter historical reports, pins, inventory, D6 fixture or residual evidence.
2. Resolve the nonprivileged test environment capacity issue using a separately approved/available filesystem, then rerun only the eight capacity/fixture failures and affected suite; report new outcomes as a distinct run.
3. Owner/reviewer reconciles official 414 inventory with candidate snapshots 426/427 and M8's 424 technical scope; issue a new versioned proposal with path/hash/status rationale, then obtain explicit approval. Do not rewrite history or silently promote.
4. Product owners address the two high security findings (shell-based `verify_sha256`, link-safe Arch extraction), the Arch option handling, and malformed legacy metadata policy in a separate implementation session; add focused malicious-path/archive regressions and then repeat M9 security review.
5. Freeze exact source, toolchain, dependencies, environment and build path for the approved candidate. Produce a fresh build and artifact manifest; conduct independent hash comparison and a reviewed pin proposal. Do not update official pins until the explicit promotion workflow approves it.
6. Conduct organizationally independent security/evidence review. If all nonprivileged gates pass and approvals exist, execute only the approved Debian VM cases; preserve raw logs/statuses and D6/evidence, then obtain independent evidence review.
7. Make a separate release decision only after every required case and approval closes. Until then retain NO-GO/BLOCKED.
