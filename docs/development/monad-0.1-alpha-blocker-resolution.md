# Monad 0.1.0-alpha.1 Blocker Resolution

Review date: 2026-10-30  
Branch: `main`  
HEAD at start: `abd6bac4b368427b76f6c98522afde928b418939`  
Version: `0.1.0-alpha.1`

## 1. Executive summary

The activation-plan persistence failure and qualification inventory failure
were corrected. A fresh isolated Release build succeeded, and the full
nonprivileged CTest selection completed with 140 PASS, 4 SKIP, and 0 FAIL.
Twenty-five privileged tests were NOT RUN.

A local package fixture extracted and built under a fresh user-owned temporary
root, but installation stopped at artifact scanning. The scanner deliberately
fails closed when it cannot establish that hidden Linux `trusted.*` xattrs are
absent. This process had no effective capabilities (`CapEff` was all zero),
including initial-namespace `CAP_SYS_ADMIN`; no privilege was requested or
used. Install, info, and remove therefore did not complete end to end.

**Verdict: `RC_BLOCKED_BY_ENVIRONMENT`.** The source and regression gates now
pass, but the required unprivileged install/remove demonstration remains
unverified in this environment. No release artifact was produced. Alpha M1
remains **NO-GO / NOT QUALIFIED** and was not exercised.

## 2. Initial defects and root causes

### Activation-plan persistence

`activation_plan_persistence_test` failed during D4 foreign-container
perturbation 3. The test changes opaque contents within a retained private
preparation directory, then verifies that a replacement directory is rejected
without changing the original evidence anchor. The implementation compared
the complete directory observation (including size, timestamps, and link
count) for private containers. Ordinary changes inside an intentionally
opaque directory changed those fields and were incorrectly reported as anchor
replacement.

### Qualification driver inventory

`qualification_driver_test` failed during setup because the active sorted
source inventory omitted 29 legitimate source and test files already present
in the checkout. The source-closure validator correctly rejected the drift;
every test case failed before exercising its own assertion. The omitted files
were ordinary active checkout inputs, not additions to historical evidence
bundles or snapshot metadata.

### Extended attributes and local installation

The scanner's xattr listing syscall succeeds, but Linux hides `trusted.*`
namespaces from this unprivileged process. Its explicit initial-user-namespace
capability check consequently refuses to claim that those attributes are
absent. The observed diagnostic was:

```text
artifact <root>: cannot establish absence of hidden trusted.* xattrs; initial-namespace CAP_SYS_ADMIN required
```

This is an environment capability limitation interacting with a deliberate
fail-closed application policy, rather than a failed `listxattr` syscall or an
unsupported-xattr-filesystem result. `/tmp` was tmpfs and the checkout was
ext4; neither yielded an explicit unsupported-xattr response that would prove
absence. The scanner documentation already describes this requirement. No
security check was bypassed or weakened.

## 3. Source and test changes

- `src/activation/plan_persistence.cpp`: for opaque, unpublished preparation
  containers, comparison now retains the original inode/mount identity, owner,
  and mode while allowing internal directory metadata to change. Published
  containers still require full observation equality and a complete inventory
  comparison. The no-follow open and existing metadata checks remain in force.
- `tests/bounded_toml_sources.txt`: added the 29 missing active files while
  preserving the existing release-renamed packaging entries and prior edits.
  No historical report, evidence record, bundle, pin, or snapshot metadata was
  changed.
- `tests/activation_plan_persistence_test.cpp` already contained the focused
  opaque-content and replacement regression from the pre-session worktree; it
  now passes against the corrected comparison. No expected result was relaxed.
- Added this report. The existing RC report was left intact.

## 4. Focused verification

Command:

```sh
ctest --test-dir /tmp/monad-alpha-blocker-20261030 --output-on-failure \
  -R '^(activation_plan_persistence_test|qualification_driver_test|artifact_scanner_test|artifact_identity_scanner_test|object_store_scanner_test)$'
```

Result: **PASS**, 5 selected; activation persistence and qualification driver
passed. `object_store_scanner_test` and `artifact_identity_scanner_test`
reported **SKIP** due to unavailable hidden trusted-xattr inspection.
`artifact_scanner_test` passed its available checks. Direct
`python3 tests/qualification_driver_test.py` also passed all 7 test methods.

## 5. Release build

- Fresh isolated directory: `/tmp/monad-alpha-final-20261030`
- Configure: `cmake -S . -B /tmp/monad-alpha-final-20261030 -G Ninja -DCMAKE_BUILD_TYPE=Release`
- Build: `cmake --build /tmp/monad-alpha-final-20261030 --parallel 4`
- Result: **PASS**, all 287 build steps. GNU C++ 14.2.0, CMake 3.31.6,
  OpenSSL Crypto 3.5.7, and LibArchive 3.7.4 were detected.
- One pre-existing warning remains in
  `tests/store_metadata_errors_test.cpp:56` for ignoring a `[[nodiscard]]`
  TOML parse result; it did not fail the build.

The complete nonprivileged suite was run in the isolated Release directory
`/tmp/monad-alpha-blocker-20261030`, built from the same final source state.

## 6. Full nonprivileged CTest results

Command:

```sh
ctest --test-dir /tmp/monad-alpha-blocker-20261030 \
  --output-on-failure -LE privileged --parallel 4
```

| Result | Count |
|---|---:|
| PASS | 140 |
| FAIL | 0 |
| SKIP | 4 |
| NOT RUN (privileged selection excluded) | 25 |

CTest duration: **123.11 seconds**. The four skipped cases were
`object_store_scanner_test`, `artifact_identity_scanner_test`,
`transaction_gc_scanner_test`, and `package_operation_scanner_test`; each
requires successful scanner inspection unavailable on this host. No privileged
test was run.

## 7. CLI smoke checks

Using `/tmp/monad-alpha-blocker-20261030/mpkg` and a fresh empty root at
`/tmp/monad-alpha-smoke-20261030`:

| Command | Exit | Result |
|---|---:|---|
| `mpkg --help` | 0 | PASS |
| `mpkg --version` | 0 | PASS; reports `0.1.0-alpha.1` |
| `mpkg --root <root> list` | 0 | PASS on empty state |
| `mpkg --root <root> generation list` | 0 | PASS on empty state |

## 8. Install/remove end-to-end fixture

A minimal local CMake source archive and package manifest were created for
`localdemo`. The fixture root was
`/tmp/monad-alpha-e2e-20261030b/root`, owned by the current user and restricted
to mode 0700. The commands were:

```sh
/tmp/monad-alpha-blocker-20261030/mpkg --root /tmp/monad-alpha-e2e-20261030b/root install localdemo
/tmp/monad-alpha-blocker-20261030/mpkg --root /tmp/monad-alpha-e2e-20261030b/root info localdemo
/tmp/monad-alpha-blocker-20261030/mpkg --root /tmp/monad-alpha-e2e-20261030b/root remove localdemo
/tmp/monad-alpha-blocker-20261030/mpkg --root /tmp/monad-alpha-e2e-20261030b/root generation current
/tmp/monad-alpha-blocker-20261030/mpkg --root /tmp/monad-alpha-e2e-20261030b/root generation list
```

| Operation | Exit | Result |
|---|---:|---|
| Install | 1 | BLOCKED at artifact scanner hidden `trusted.*` check |
| Info | 1 | Package not installed |
| Remove | 1 | Package not installed |
| Generation current | 0 | Generation 0 |
| Generation list | 0 | Generation 0, 0 packages |

The archive extracted and the CMake build/install-to-staging steps completed.
The active generation remained empty. A transaction pin/lease record was left
inside the disposable store after the failed publication attempt. All observed
manager-created build, generation, and store paths were under the fixture root;
the source archive and package manifest were intentional fixture inputs
outside it. No host-root or system-wide path was targeted. Generation switching
and rollback were not run because there was no installed generation to switch
to or roll back from. This is **not** an install/remove end-to-end pass.

## 9. Remaining limitations

- This environment cannot validate the supported successful install path while
  the scanner requires initial-namespace `CAP_SYS_ADMIN` to establish hidden
  trusted-xattr absence. No safe no-capability narrowing was established, so
  production checks were preserved.
- Install/remove, installed-package info, generation switch, and rollback are
  not verified end to end.
- Scanner-dependent CTest cases remain skipped; the 25 privileged tests remain
  unrun by instruction.
- This review does not qualify Alpha M1. It remains **NO-GO / NOT QUALIFIED**.

## 10. Release gate

| Criterion | Result | Evidence |
|---|---|---|
| A. Release build PASS | PASS | Fresh isolated 287-step Release build |
| B. CLI basic smoke PASS | PASS | Help, version, list, empty generation list |
| C. Activation persistence PASS | PASS | Focused and full-suite tests pass |
| D. Install/remove end to end PASS | UNKNOWN | Installation fails closed at trusted-xattr inspection |
| E. No writes outside test root | PASS (observed) | Manager-created build/store/generation paths stayed in disposable root |
| F. No known P0 in release scope | PASS (observed checks only) | No P0 observed; privileged checks were not run |
| G. No known P1 in release scope | UNKNOWN | Core defects fixed; essential install/remove operation remains unverified |
| H. Full nonprivileged suite PASS, documented skips excluded | PASS | 140 PASS, 4 SKIP, 0 FAIL |
| I. Version remains `0.1.0-alpha.1` | PASS | CLI smoke output |
| J. Documentation reflects actual behavior | PASS | Scanner docs and this report state the capability requirement and limits |

The release gate does not pass because criteria D and G remain unknown. No
release-ready archive or other release artifact was prepared.

## 11. Artifacts and checksums

No release staging directory, source archive, release manifest, checksum, or
release notes bundle was produced. The gate did not pass. Temporary build and
fixture paths are verification outputs only, not release artifacts.

## 12. Git diff summary and preservation

The baseline worktree was already heavily modified and contained numerous
untracked build trees, reports, qualification files, scripts, and evidence.
Those items were preserved. The changes attributable to this blocker session
are the D4 comparison correction, the active inventory additions, and this
new report. The inventory file also retains pre-existing renames from Athena
packaging names to Monad names. No commit, tag, push, publication, privileged
operation, or historical evidence modification was made.

## 13. Final verdict and next action

**`RC_BLOCKED_BY_ENVIRONMENT`**. The two reported test defects are resolved and
the nonprivileged suite passes with four documented skips, but the required
unprivileged install/remove proof could not complete without weakening the
scanner's security policy.

Next action: run the existing install/remove fixture in an approved ordinary
environment where the scanner can establish complete xattr metadata without
granting this process additional capabilities, then repeat the end-to-end
checks and release gate. Alpha M1 qualification remains a separate campaign.
