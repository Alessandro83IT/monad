# Monad 0.1 Alpha M1 — package manager audit and stabilization

**Disposition: NO-GO. Alpha M1 remains BLOCKED.** This report records a local,
unprivileged source audit and test run. It does not qualify the release or the
administrative gateway.

## Scope and starting state

- Branch `main`, initial `HEAD` `abd6bac4b368427b76f6c98522afde928b418939`.
- The worktree already had modified CMake/test files, untracked qualification
  reports and source files, and several populated build directories. Those
  artifacts were preserved. No reset, clean, commit, push, installation,
  collector, host Session 68 driver, credential access, or privileged command
  was performed.
- All new compilation output went to
  `/tmp/monad-m1-package-audit-build`.

## Implemented behavior and evidence

The source contains an `mpkg` CLI, legacy package metadata loading, bounded TOML
parsing for versioned operational documents, dependency expressions and
resolution, local and Arch repository adapters, archive acquisition and
extraction, Autotools/CMake builders, SHA-256 verification, package store and
metadata, install/remove planning, immutable artifacts, generation publication,
selection, rollback, and request recovery. The newer CLI install path uses the
repository/realization APIs; the older public installer path remains present.

Evidence is not interchangeable:

- The current local run built Debug successfully and ran 124 selected CTest
  cases. It recorded **116 PASS, 1 FAIL, 7 SKIP, 0 TIMEOUT**. The sole failure is
  described below. Six scanner-named tests, 18 `privileged`-labelled tests, and
  one diagnostic-labelled test were excluded before execution.
- The B4 report in
  `docs/qualification/m2.5-d8-b4-20261016-4/` records a separately hashed,
  historical source bundle with Debug/Release internal API results. Its report
  explicitly limits the verdict: the positive production-scanner case and
  privileged legacy cases were not physically verified. Its recorded baseline
  is `c0a8b125...`, not this worktree's current `HEAD`; it is historical evidence,
  not proof for this current source set.
- Alpha M1 Sessions 67–69 report synthetic supervisor/orchestration checks.
  They do not establish a real Debian VM run or privileged package-manager
  qualification. Session 69 did not fetch from live GitHub or launch Codex.

## Defects corrected

1. **Shell command injection before archive integrity verification.** Manifest
   `source` and filesystem paths were interpolated into `std::system()` strings
   in downloader, tar extraction, and CMake/Autotools backends. A crafted URL or
   directory name could execute shell syntax as the invoking user. A shared
   direct-argv process runner now invokes those tools without shell parsing.
   `curl` receives `--` before the URL. Regression tests use a fake downloader
   and extractor and a CMake source path containing command substitution; no
   marker command is created.
2. **Package identity path traversal.** Package names and versions reached
   store/build path construction without a common filesystem-safe validation
   boundary. `package::validate_identity` now validates names and parses
   versions before the loader returns or store lookup/install constructs a path.
   Local repository errors retain the `InvalidCandidate` classification.

The remaining shell calls in the Arch adapter use its single-quote argument
encoder; those paths were inspected but not changed. Source builds still execute
package-provided configure/CMake/make logic with the invoking user's authority;
this session did not add a build sandbox.

## Files changed for this session

- `src/process/run.hpp`, `src/process/run.cpp` — direct argv process runner.
- `src/download/downloader.cpp`, `src/archive/archive.cpp` — shell-free URL and
  archive argument handling.
- `src/build/cmake_backend.cpp`, `src/build/autotools_backend.cpp` — shell-free
  configure/build/install invocations.
- `src/package/package.hpp`, `src/package/package.cpp`,
  `src/package/package_loader.cpp`, `src/core/local_repository.cpp`,
  `src/store/store.cpp` — package identity checks at metadata and path sinks.
- `tests/process_invocation_test.cpp`, `tests/cmake_backend_test.cpp`,
  `tests/package_loader_test.cpp`, `CMakeLists.txt` — regression coverage and
  registration. `CMakeLists.txt` and `tests/cmake_backend_test.cpp` already had
  user changes before this session; those edits were preserved.

## Verification results

| Check | Result | Details |
|---|---|---|
| Fresh Debug configure and complete build | **PASS** | GCC 14.2.0; isolated `/tmp` build. After the final identity edit, the core and affected test targets were rebuilt. |
| `process_invocation_test`, `cmake_backend_test`, `package_loader_test`, `installer_e2e_test`, `store_metadata_test`, `local_repository_test` | **PASS — 6/6** | Rerun after the final identity-validation change. |
| Broad unprivileged CTest selection | **FAIL — 1** | `qualification_driver_test` errored in all seven cases because the checked-in input inventory does not match the current source tree. It reports pre-existing unlisted Alpha M1 Python tests and this session's new process-runner/test files. The inventory was not rewritten because it is a qualification baseline and already contained user edits. |
| Broad CTest skipped cases | **SKIP — 7** | Installed semantic, CLI install, install transaction, generation activation, realization executor, removal realization, and immutable object publication integration cases returned 77: the scanner could not establish absence of hidden `trusted.*` xattrs without initial-namespace `CAP_SYS_ADMIN`. |
| Broad CTest timeouts | **0** | No selected test timed out. |
| 18 privileged-labelled tests, six scanner tests, diagnostic case | **NOT RUN** | Excluded before invocation; no CAP_SYS_ADMIN-dependent test was attempted. |
| Live network, installation, Alpha M1 collector, Session 68 host driver | **NOT RUN** | Explicitly outside this run. |
| `git diff --check` | **PASS** | Tracked diff whitespace check; new files were also reviewed with the code changes. |

**Selection oversight:** the seven skipped integration tests are not labelled
`privileged`, so the broad selection invoked them. Each stopped immediately
before its privileged operation with `cannot establish absence of hidden
trusted.* xattrs; initial-namespace CAP_SYS_ADMIN required`. No privileged
operation ran, but invoking these cases did not comply with the requested test
filter. They remain SKIP, are not qualification evidence, and were excluded from
all subsequent test runs.

CTest command for the broad run:

```sh
ctest --test-dir /tmp/monad-m1-package-audit-build \
  --output-on-failure -LE 'privileged|diagnostic' -E 'scanner' -j2
```

The inventory failure is release-relevant: the current source closure is not
reproducibly qualified until the owner reviews and reconciles the exact candidate
input inventory. It was not converted into a pass.

## Alpha requirements matrix

| Requirement | State | Evidence / remaining work |
|---|---|---|
| CLI and package operations | **IMPLEMENTED / PARTIALLY VERIFIED** | Current nonprivileged CLI, install, remove, generation, and package-operation tests passed where selected; seven integrations skipped. |
| Manifest TOML parsing and validation | **IMPLEMENTED / PARTIALLY VERIFIED** | Versioned bounded parser is present; legacy package loader is more permissive for wrong field types. Identity/path validation is now covered, but a complete strict manifest-schema audit remains. |
| Dependency resolution and repository metadata | **IMPLEMENTED / PARTIALLY VERIFIED** | Resolver, local repository, Arch adapter, constraints, and snapshot tests exist. Live repository/network acquisition was not run. |
| Download, archive extraction, CMake/Autotools build | **IMPLEMENTED / VERIFIED LOCALLY** | Shell injection paths corrected; argv regression and CMake staging tests passed. No live package download or untrusted source build was run. |
| SHA-256, artifact store, install/remove | **IMPLEMENTED / PARTIALLY VERIFIED** | Local store/install tests passed. Production scanner physical authorization was excluded and remains unverified. |
| Generation activation, switch, rollback, recovery | **IMPLEMENTED / PARTIALLY VERIFIED** | Current selected state-machine/fault-injection tests passed. Skipped integration cases and unrun VM/privileged cases remain outside this evidence. |
| Current source closure and reproducible candidate | **BLOCKED** | `qualification_driver_test` fails because its inventory does not cover the worktree source set. Resolve without silently changing historical evidence. |
| Alpha M1 admin staging and privileged campaign | **BLOCKED** | Approved Debian VM campaign, all exact required privileged cases, and evidence review are outstanding per the staging/recovery runbooks. |
| Current binary/security pins | **BLOCKED** | C++ source changes alter candidate build identity. Rebuild and reconcile pins only through the reviewed promotion process; no pin was regenerated here. |
| Release decision | **NO-GO** | Qualification gates above remain open. Alpha M1 is still BLOCKED. |

## Residual issues, severity, and dependencies

1. **Critical release gate — privileged Alpha M1 campaign and evidence review.**
   Requires the approved disposable Debian VM and separately reviewed admin
   workflow. Until then M1 stays BLOCKED.
2. **High — source inventory mismatch.** Review the exact current source set,
   including pre-existing untracked candidate files, then create a reviewed
   qualification inventory/evidence run. Do not edit historical results in
   place.
3. **High — candidate binary hashes and pins.** Rebuild and conduct the existing
   security-sensitive promotion review after the source set is settled. Never
   derive new administrative pins from this local build without that review.
4. **Medium — manifest schema strictness.** The legacy package loader silently
   ignores some wrong-typed optional arrays/entries. Tighten schema errors and
   add malformed-manifest cases before treating repository metadata as strict.
5. **Medium — physical scanner/filesystem proof.** The B4 evidence itself says
   the production-scanner positive case and privileged legacy sealing/adoption
   are not physically proven here. Resolve on the approved qualification host.
6. **Medium — build trust boundary.** Package build scripts execute as the
   invoking user without an execution sandbox. Define the release trust model
   and supported build isolation before accepting untrusted recipes.

## Recommended next macrosessions

1. **M1-A: source and candidate closure.** Reconcile the working tree, review
   each candidate input, decide the supported manifest schema, and produce a
   cleanly inventoried Debug/Release candidate without altering historical
   bundles.
2. **M1-B: package-manager closure.** Complete malformed manifest tests, review
   extraction/path behavior and build trust, run applicable nonprivileged
   integration cases, and rebuild candidate binaries.
3. **M1-C: reviewed Debian VM qualification.** Reconcile all current pins, follow
   the approved staging runbook, execute only the enumerated privileged cases,
   preserve all fixtures/logs/receipts/backups, and independently review results.
4. **M1-D: release decision.** Recompute the candidate manifest and hashes from
   the reviewed source, close every required case, and decide GO/NO-GO from the
   preserved evidence. Any skip, mismatch, timeout, or missing receipt keeps
   Alpha M1 BLOCKED.

No qualification baseline, installer pin, administrative path, or historical
evidence was changed by this session. No commit or push was made.
