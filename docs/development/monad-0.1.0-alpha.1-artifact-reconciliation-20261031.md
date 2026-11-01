# Monad 0.1.0-alpha.1 artifact reconciliation

Date: 2026-10-31
Repository HEAD: `668b5515d4e6ae72eb4074e14b3b558d8d3d148f` (`main`)
Working tree: pre-existing edits to `CMakeLists.txt` and
`tests/bounded_toml_sources.txt`, plus untracked release tooling and evidence.
Those inputs were preserved and used as found.

## Finding

The three supplied digests identify different objects or claims:

| Digest | Reconciliation |
|---|---|
| `6ebd8413d03396591c32ad6d82152fde42ca4ed3a18700d6f41adceb3dc55ed9` | Measured from the pre-existing `.codex-alpha-release-20261031/stage/bin/mpkg`. Its corresponding staged core library is `2d23dfcabc06a965a068f716be9fda26b465226f675c23eab4328061271ca2be`. The old stage has group-writable directories (0775), so it is not suitable for qualification. |
| `82cb67e19818c0f6d6af450af75ae76b3c7c1951373e1592b48269498ae93c21` | Measured from `.codex-alpha-release-20261031/mpkg`, the build-tree executable. Its RUNPATH is an absolute path into that build directory. It is not the installed/staged executable and is not the qualification artifact. |
| `c9b06dcb9621e50175afab4cefc090313aab9b36665047b48f20a76b717c926f` | The previous release-preparation document and launcher called this a build measurement/pin. No file matching it was found among the extant `mpkg` artifacts inspected in the repository. The record does not include a source path or measurement receipt, so its claimed provenance cannot be independently established. It does not match either supplied artifact. It remains historical documentation, not an approved digest. |

The current tree was configured as a fresh GNU C++ Release build at
`/tmp/monad-alpha1-reconcile-20261031`, with a separate CMake install prefix
`.codex-alpha-release-candidate-20261031/stage`. No previous build, staging
tree, VM, or qualification evidence was overwritten. The build used the
repository HEAD above plus the already-present working-tree edits. CMake
release version is `0.1.0-alpha.1`; `athena_install_targets` specifies the
Linux executable install RUNPATH as `$ORIGIN/../lib` for this prefix layout.

## New isolated measurements

All values below were measured locally after installation; they are not
cryptographic attestations or release pins.

| Object | SHA-256 |
|---|---|
| Build tree `mpkg` | `87acbaab7a770a63ab3a1114ff05dd014706279119c28a5b2b3538dbd15c2520` |
| Staged `bin/mpkg` | `bd20e79efe91a914a811f06eb3c3cfdb157ff7e525c7854415c89732b24e0e8f` |
| Build tree `libathena_core.so` | `2d23dfcabc06a965a068f716be9fda26b465226f675c23eab4328061271ca2be` |
| Staged `lib/libathena_core.so` | `2d23dfcabc06a965a068f716be9fda26b465226f675c23eab4328061271ca2be` |

The build-tree executable reports `Monad Package Manager 0.1.0-alpha.1` and
has a build-local absolute RUNPATH. The installed executable reports the
same version, depends on `libathena_core.so`, and has RUNPATH exactly
`$ORIGIN/../lib`; the installed layout therefore resolves its sibling library
without embedding the build path. The staged core SONAME is
`libathena_core.so`. CMake install completed successfully.

The new stage is owned by the invoking user. Directories `stage`, `bin`,
`lib`, and `include` are mode 0755; `bin/mpkg` is 0755 and the library is
0644. No group/world write bits or symlinks are present in the runtime stage.
The staged executable's hard-link count is one.

## Verification and verdict

The fresh build and installation completed successfully. `packaging_install_test`
passed in the nonprivileged CTest selection. The selection command was
`ctest --test-dir /tmp/monad-alpha1-reconcile-20261031 --output-on-failure
-LE privileged -E '^artifact_scanner_test$'`: 148 selected, 144 PASS, 4 SKIP,
0 FAIL, 0 TIMEOUT (409.06 seconds). The skipped cases were
`object_store_scanner_test`, `artifact_identity_scanner_test`,
`transaction_gc_scanner_test`, and `package_operation_scanner_test`; each
reported unavailable host xattr/capability preconditions. `artifact_scanner_test`
was explicitly excluded and is NOT RUN. Privileged-labeled cases are NOT RUN.
The three E2E guard tests, launcher test, qualification driver, version test,
and packaging tests passed in this selection.

The nonprivileged launcher `--check` exited 2 with BLOCKED and created no
evidence directory. Current blockers were denied access to guest product UUID,
possibly connected `enp0s3`, missing effective `CAP_SYS_ADMIN`, and `/var/tmp`
mounted read-only. No privileged harness or E2E was launched.

**Verdict: PASS WITH CONDITIONS** for artifact reconciliation and selection
of a qualification candidate. The candidate's build, install layout, measured
identity, stage integrity, and relevant nonprivileged tests pass. It is not a
release qualification result: host/clone preconditions remain blocked, and
all privileged tests and E2E remain NOT RUN. Qualification may proceed only
in the independently reviewed disposable clone under the documented procedure.

## Candidate and limits

The only artifact selected for further Alpha.1 qualification is
`.codex-alpha-release-candidate-20261031/stage/bin/mpkg`, with its adjacent
`stage/lib/libathena_core.so`. It is a reviewable **qualification candidate**
because it comes from a fresh isolated Release configure/build/install, has
the expected version and relocatable install RUNPATH, and its staged tree
passes the ownership/mode/hash checks. The old stage, build-tree executable,
and undocumented c9 digest are rejected as qualification inputs.

This decision does not qualify a release or approve privileged execution.
The host-side checks show the current environment lacks the required isolated
clone evidence, capability/namespace, writable `/var/tmp`, and network
isolation conditions. Privileged CTest and E2E remain NOT RUN. A release
qualification requires the approved disposable clone procedure and its
separate evidence review.

The launcher and its hash expectation test now point only to the new stage
and its measured `bd20…` digest. No old measurement was rewritten. The prior
release-preparation document and its historical c9 claim were left intact.
