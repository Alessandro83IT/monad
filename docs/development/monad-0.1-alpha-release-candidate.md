# Monad Package Manager 0.1.0-alpha.1 Release Candidate Review

Review date: 2026-10-30  
Branch: `main`  
HEAD at preflight: `abd6bac4b368427b76f6c98522afde928b418939`

## 1. Executive summary

The source configures and builds in Release mode with GCC 14.2.0. The CLI
reports `0.1.0-alpha.1`, and read-only commands work with a user-owned `--root`.
The release gate is **not met**: two non-privileged CTest cases fail, and the
local package install demonstration stops at artifact scanning because this
environment cannot establish the absence of `trusted.*` xattrs without
`CAP_SYS_ADMIN`. The package install/remove path is therefore not demonstrated
as usable without privilege.

**Verdict: `RC_BLOCKED_BY_DEFECTS`.** One failure reproduces in the activation
plan persistence evidence test; the other is a stale qualification source
inventory versus files already present in the checkout. The install-path check
is additionally blocked by the environment. No source archive, staging tree,
binary release, checksum manifest, tag, or publication was produced.

This is a separate experimental release review. Alpha M1 remains
**NO-GO / NOT QUALIFIED**; no collector or privileged qualification was run.

## 2. Functional scope

| Feature | Status | Evidence and limits |
|---|---|---|
| `mpkg --help`, `mpkg --version` | READY | Both run; help and version report `0.1.0-alpha.1`. |
| `mpkg list` | EXPERIMENTAL | Runs against a fresh disposable `--root`; empty state prints no package rows. |
| `mpkg info` | EXPERIMENTAL | Implemented for installed package metadata; the CLI path was not reached in the local install attempt. |
| TOML package manifest | EXPERIMENTAL | `package_loader_test` and bounded TOML tests pass. Legacy schema omission remains accepted. |
| Dependency resolution | EXPERIMENTAL | Resolver and constraint unit tests pass; repository integration remains limited. |
| CMake build backend | EXPERIMENTAL | Backend tests pass. CLI realization only supports one local gzip tar source, CMake, and no build parameters. |
| Store / object store | EXPERIMENTAL | Core tests pass; scanner cases requiring trusted xattr inspection are skipped or blocked. |
| Install | BLOCKED | Implementation exists, but the disposable-root CLI attempt stopped at artifact scanning: `cannot establish absence of hidden trusted.* xattrs; initial-namespace CAP_SYS_ADMIN required`. |
| Remove | BLOCKED | Not run after install failed; removal integration cases are marked privileged. |
| Generation list/current | EXPERIMENTAL | Core generation tests pass; empty generation list works with a temporary root. |
| Activation | EXPERIMENTAL | Core activation tests pass. End-to-end activation in the available unprivileged environment was not established. |
| Generation switch / rollback | EXPERIMENTAL | Core generation tests cover state behavior; CLI mutation was not run because an installed environment could not be created in the non-privileged trial. |
| Network source acquisition, Autotools, binary import, Athena IR | INCOMPLETE | Unsupported by the current realization route. |

The package manager is not production-ready and is not presented as a reliable
replacement for pacman, emerge, or Nix.

## 3. Changes made in this session

- Added the package-facing CMake release string `ATHENA_RELEASE_VERSION=0.1.0-alpha.1` while retaining CMake's numeric `project(VERSION 0.1.0)` compatibility version.
- Updated CLI help and `--version` to use the package-facing version string.
- Updated README status, dependency requirements, build/test commands, disposable-root example, and supported/unsupported realization notes.
- Added this report. No historical identifiers, qualification pins, collector code, or evidence were intentionally changed.

Pre-existing edits were present at preflight in CMake, implementation and test
files, plus numerous untracked documents, scripts, and build trees. They were
preserved. `CMakeLists.txt` was already modified before this session; the
release-version definition was added without replacing its existing test and
label changes.

## 4. Build

- Build directory: `/tmp/monad-rc-20261030-build` (fresh isolated directory).
- Generator/configuration: Ninja, `Release`.
- Compiler: GNU C++ 14.2.0 (Debian 14.2.0-19).
- CMake: 3.31.6.
- Dependencies discovered: OpenSSL Crypto 3.5.7, LibArchive 3.7.4, Threads,
  Python 3.13.5.
- Result: **PASS**. Configure succeeded and all 287 initial build steps
  completed; the version change was then rebuilt successfully.
- Warning observed: one `-Wunused-result` warning in
  `tests/store_metadata_errors_test.cpp` for ignoring the TOML parser result.
  No build errors were observed.
- No system installation was performed.

Built CLI SHA-256 (local test build only):

```text
d4f8dccb816ae197806eb209a67b35c56976c5755a54d07b380b1273257c90e3  /tmp/monad-rc-20261030-build/mpkg
```

This binary is not a release artifact and is outside the repository.

## 5. Tests and command checks

Command: `ctest --test-dir /tmp/monad-rc-20261030-build --output-on-failure -LE privileged --parallel 4`

| Result | Count | Notes |
|---|---:|---|
| PASS | 138 | Includes parser, dependency, backend, store, activation, generation, removal-planning, packaging, and other non-privileged cases. |
| FAIL | 2 | `activation_plan_persistence_test`; `qualification_driver_test`. |
| SKIP | 4 | `object_store_scanner_test`, `artifact_identity_scanner_test`, `transaction_gc_scanner_test`, `package_operation_scanner_test` (their declared skip path). |
| NOT RUN | 25 | Cases labelled `privileged`, excluded by the command. No privileged test was run. |

`activation_plan_persistence_test` was rerun alone and failed again. Its
`evidence` subcase reproduces after the four D4 foreign-substitution perturbation
cases, reporting `Foreign container anchor changed`. This remains an unresolved
test/behavior defect; no fix is claimed.

`qualification_driver_test` failed because the existing source-closure inventory
does not list files already present in this checkout, including
`src/process/run.cpp`, `src/process/run.hpp`, and multiple untracked test files.
The inventory and forensic qualification workflow were not changed as part of
this release review.

CLI checks:

- `mpkg --help`: **PASS**.
- `mpkg --version`: **PASS**, output `Monad Package Manager 0.1.0-alpha.1`.
- `mpkg --root <temporary-root> list`: **PASS** on empty state.
- `mpkg --root <temporary-root> generation list`: **PASS** on empty state.
- Disposable local CMake package install: **BLOCKED BY ENVIRONMENT** at the
  artifact scanner's `trusted.*` xattr check requiring `CAP_SYS_ADMIN`.
- `info`, successful CLI install, CLI remove, CLI generation switch/rollback:
  **NOT RUN** because the non-privileged install trial could not establish an
  installed package.

No Alpha M1 collector was run.

## 6. Defects corrected

The version mismatch between CMake-facing metadata and CLI output was corrected.
The CLI previously displayed `0.1.0` while this requested candidate is
`0.1.0-alpha.1`. Help and version now share the CMake-provided release string.
The change was verified by rebuilding and running both CLI options.

No regression test was added for this display-only version correction; the
existing CLI smoke was run directly. No other defect is claimed fixed.

## 7. Residual defects and limitations

- `activation_plan_persistence_test` has a reproducible failure in its D4
  foreign-container evidence checks.
- `qualification_driver_test`'s static source inventory is inconsistent with
  files in this checkout; this prevents that test from passing in the current
  source state.
- The install scanner cannot complete in this environment without
  `CAP_SYS_ADMIN`; the required no-privilege demonstration is not established.
- Remove, installed-package info, and CLI switch/rollback were consequently not
  demonstrated end to end.
- The supported package realization route is narrow: one local gzip tar source,
  CMake, and no build parameters. Remote acquisition, Autotools, binary import,
  Athena IR, and broader compatibility remain unsupported.
- The CLI help does not document every subcommand or the narrow install recipe
  limits; README and implementation documents supply context but the UX remains
  experimental.

## 8. Versioning

- Requested package version: `0.1.0-alpha.1`.
- CMake numeric project version: `0.1.0`.
- CMake package/CLI release string: `ATHENA_RELEASE_VERSION` = `0.1.0-alpha.1`.
- CLI `--version` and help: `0.1.0-alpha.1`.
- README: `0.1.0-alpha.1`.
- No separate package metadata or source-package version file exists.
- No tag or commit was created.

## 9. Documentation

README now describes the experimental status, requirements, build/test flow,
non-privileged disposable-root commands, and the limited realization route.
Existing install, realization, and installed-state implementation notes remain
the detailed references. No compatibility promise beyond tested configurations
is made.

## 10. Release artifacts and checksums

No release staging directory or source archive was created because the release
gate failed. Consequently there is no source archive, artifact manifest,
archive SHA-256, changelog bundle, release notes bundle, or distributable binary
to verify. The hash above identifies only the local build used for checks.

No credential, private file, build cache, or forensic evidence was copied into
a release artifact; no release artifact was made.

## 11. Risks

- The activation persistence failure concerns security-sensitive state evidence
  and needs diagnosis and a passing regression test before release review can
  proceed.
- A normal unprivileged user cannot complete the attempted install path in this
  environment due to the scanner's trusted xattr inspection requirement.
- Running package build scripts executes package-controlled build logic as the
  current user. Trial only with a disposable root and trusted local fixtures.
- Existing preflight changes were not authored or independently reconciled in
  this session; the source closure failure is consistent with that pre-existing
  worktree state.

## 12. Release gate

| Gate | Result | Basis |
|---|---|---|
| A. Clean build | PASS | Fresh isolated Release build completed. |
| B. CLI functioning | PARTIAL | Help/version/list/empty generation work; install path does not complete. |
| C. Critical non-privileged tests | FAIL | Two CTest failures; 4 additional declared skips. |
| D. No unmitigated P0 in scope | NOT ESTABLISHED | No P0 observed in executed checks; security-sensitive persistence failure remains unresolved. |
| E. No blocking P1 | FAIL | Reproduced activation persistence failure blocks this gate. |
| F. Version consistency | PASS | CMake release string, CLI, and README agree. |
| G. Sufficient documentation | PARTIAL | Trial and limits documented; command help and end-to-end install guidance remain sparse. |
| H. Verifiable source archive | NOT MET | Archive withheld because tests failed. |
| I. Explicit limitations | PASS | README and this report state supported path and known limits. |
| J. No privilege needed for demo | FAIL | Artifact scan required `CAP_SYS_ADMIN` in the attempted local install. |

## 13. Verdict and publication work

**`RC_BLOCKED_BY_DEFECTS`**. The source is buildable, but this checkout does not
meet the release gate and no RC package should be published from this review.
The install demonstration also has an environment blocker.

Before reconsidering publication:

1. Diagnose and fix the activation persistence failure, with a passing focused
   regression.
2. Reconcile the qualification source inventory with the intended source set
   through its reviewed workflow, without treating that as Alpha M1
   qualification.
3. Demonstrate install, info, remove, and generation switch/rollback using a
   non-privileged disposable root, or explicitly redesign/document the scanner
   requirement if that operation cannot be unprivileged.
4. Rerun the non-privileged suite and CLI checks, then generate and inspect a
   curated source archive, manifest, and SHA-256 list.
5. Have an operator review and separately authorize any eventual publication.

No commit, tag, push, publication, privileged operation, or Alpha M1
qualification was performed.
