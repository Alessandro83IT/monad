# Monad 0.1 Alpha M10 — remediation and requalification

**Decision: NO-GO. Alpha M1 remains BLOCKED.** This work was performed on `main`, starting at `abd6bac4b368427b76f6c98522afde928b418939`. No privileged tests, installations, scanners, collectors, Session 68 host driver, baseline/inventory/pin changes, commits, or pushes were performed. Existing worktree state and evidence were preserved.

## Scope and changes

Read the complete M9 report and the M3 report it references. M9 documented a shell-based SHA-256 check in `src/verify/verifier.cpp`, unsafe Arch archive extraction, and shell-based Arch helper commands. The SHA-256 implementation was changed to stream file bytes through OpenSSL EVP SHA-256, avoiding shell execution, pathname interpretation and output parsing. The existing OpenSSL Crypto dependency is used.

**Incomplete remediation:** the Arch paths were inspected but not yet converted. `src/repository/arch_repository.cpp` still invokes `curl`, `tar`, `sha256sum`, and `gpgv` through shell commands; package extraction still uses tar after pathname-only checks. Traversal, symlink, hardlink, special-file, duplicate, size-limit and corrupted-archive defenses equivalent to M3 are therefore NOT implemented or verified. No hostile-input regression was added for these paths or for the new digest implementation. This is a security blocker.

The change to `src/verify/verifier.cpp` changes the technical source closure. The historical M8 closure manifest, official inventory and installer pins were not modified or regenerated. M8 closure equivalence and official qualification inventory are consequently NOT established for this source state; source closure requires a reviewed, versioned reconciliation. The historical 414-entry driver mismatch remains separate and was not addressed by changing its baseline.

## Capacity and requalification

Before running, `df -h` showed the project filesystem with 93 GiB available, while `/tmp` was 3.9 GiB full with 12 MiB available (100%); inode availability was ample. A new private directory `.m10-tmp-20261027/` was created on the project filesystem and passed as `TMPDIR`. It was not sufficient because the affected fixtures use fixed `/tmp` paths rather than honoring `TMPDIR`. No pre-existing `/tmp` artifacts were removed or modified intentionally.

| Check | Result | Time |
|---|---|---:|
| Incremental full build in `.m9-independent-build` | PASS | ~2.4 s observed tool wait; build completed |
| `operational_request_runtime_test` | FAIL — `fixture write` | 0.13 s |
| `activation_plan_persistence_test` | FAIL — `fixture write` after test diagnostics | 5.11 s |
| `transaction_gc_test` | FAIL — `write fixture` | 0.04 s |
| `activation_plan_retention_limits_test` | FAIL — `write fixture` | 0.05 s |
| `activation_plan_retention_budgets_test` | FAIL — `write fixture` | 0.13 s |
| `activation_plan_persistence_bounds_test` | FAIL — `No space left on device` | 2.06 s |
| `activation_plan_persistence_publication-capacity_test` | FAIL — `fixture write` | 0.04 s |
| `activation_plan_persistence_recovery-capacity_test` | FAIL — `fixture write` | 0.04 s |

All eight requested cases were repeated and all eight remain FAIL. `TMPDIR` isolation did not redirect hard-coded `/tmp` fixtures. Test output is in `.m9-independent-build/Testing/Temporary/LastTest.log` (CTest run). `qualification_driver_test` was kept separate and NOT RUN; its known historical inventory mismatch was not changed. The broader nonprivileged suite was NOT RUN because `/tmp` is full and test temp paths do not honor the isolated directory. Privileged tests, unauthorized scanners and collectors were NOT RUN.

## Alpha requirements matrix

| Requirement | M10 status |
|---|---|
| Remove shell from SHA-256 verification | Partial PASS: verifier path uses OpenSSL; hostile-path regression NOT RUN/NOT ADDED; Arch SHA path remains shell-based |
| Harden Arch extraction to M3-equivalent controls | BLOCKED: implementation and adversarial validation outstanding |
| Repeat eight `/tmp`-capacity failures | FAIL: 0 PASS, 8 FAIL; isolated `TMPDIR` cannot override fixed `/tmp` paths |
| Nonprivileged qualification suite | NOT RUN (capacity constraint) |
| `qualification_driver_test` | NOT RUN; inventory mismatch remains known and separate |
| Approved source closure, inventory and pin alignment | BLOCKED; no historical records changed; changed verifier is outside M8 byte closure |
| Debian VM privileged campaign and evidence review | BLOCKED / NOT RUN, as required by session constraints |

## Residual risks and Debian VM blockers

Arch command injection/option interpretation and unsafe link-following extraction remain unresolved. Digest behavior has no dedicated regression evidence. Metadata schema strictness, power-loss durability/cross-namespace recovery, unsandboxed package build scripts, unauthenticated build identity/provenance, source/inventory divergence and historical pin mismatch remain as recorded in M9. The official qualification inventory mismatch remains untouched. The eight capacity cases did not pass, the broader nonprivileged suite was not completed, and no approved candidate closure exists.

Before a Debian VM campaign: resolve the full `/tmp` condition without deleting existing evidence (or adapt fixtures through an explicitly scoped test change), complete Arch hardening and hostile fixtures, add hostile-path SHA regressions, rerun these eight cases and the allowed nonprivileged suite with separate statuses/timings, obtain reviewed source-closure/inventory reconciliation and pin approval through the established workflow, and complete independent evidence/security review. Privileged VM work remains outside this session.

**GO/NO-GO: NO-GO. Alpha M1 remains BLOCKED.** No evidence here qualifies Alpha M1 or authorizes installation, pin promotion, privileged testing, or release.
