# Monad 0.1.0-alpha.1 release preparation

Date: 2026-10-31
Source `HEAD` before these edits: `668b5515d4e6ae72eb4074e14b3b558d8d3d148f`
Target guest, as supplied for this task: `Monad-Alpha-Release-Test`
Original guest: `Monad-Alpha-M1` (must remain unchanged)

## Changes

- Reviewed `tests/monad_alpha_e2e_guard_test.py`: its three cases exercise the
  wrong-token, parent-traversal, and symlink-component guards. Each assertion
  checks rejection before the harness can create its area or reach the
  capability check. The file does not invoke `mpkg` or perform privileged
  operations. Removed its unused `os` import.
- Added the test file once to the sorted, unique
  `tests/bounded_toml_sources.txt` input inventory.
- Registered the three unittest methods independently with CTest, each with a
  30-second timeout. The existing CMake edits and all historical evidence were
  preserved.

## Clone isolation assessment

The task identifies the active guest as a complete, independent clone named
`Monad-Alpha-Release-Test`; it says the original `Monad-Alpha-M1` must not be
changed. Read-only checks in this session reported hostname `debian`, Debian
user `alessandro`, and `systemd-detect-virt --vm` result `oracle` (VirtualBox).
These checks establish that the command environment is virtualized, but do not
independently bind it to the named VirtualBox guest or prove disk, snapshot,
network, or evidence-store independence from the original. No VM management
metadata or original guest was accessed. Treat the supplied clone identity as
the operator's context, not as independently verified provenance.

Before any privileged E2E run on the clone, the operator should verify in the
VirtualBox manager that the selected guest is `Monad-Alpha-Release-Test`, its
VM UUID differs from `Monad-Alpha-M1`, and its writable virtual disks are
clone-owned and not shared with the original. Confirm the clone has no mounted
host path to original-VM evidence, use a fresh owner-only empty area beneath
`/var/tmp`, and preserve the resulting evidence in a clone-specific location.
Do not run the harness against `Monad-Alpha-M1`.

## Clone-specific privileged E2E procedure (prepared, not run)

1. On the clone only, record guest name/UUID, Debian and kernel versions,
   filesystem and mount options for `/var/tmp`, free space, and the exact
   binary source/revision. Verify the reviewed binary reports exactly
   `Monad Package Manager 0.1.0-alpha.1` and record its SHA-256.
2. Create a new, unique, caller-owned mode-0700 empty directory beneath
   `/var/tmp`; do not reuse a prior attempt's path. Establish the harness's
   required initial-user-namespace effective `CAP_SYS_ADMIN` for the exact
   test process through the clone's separately reviewed operating procedure.
   A nested user namespace is not sufficient.
3. After identity, disk/evidence isolation, filesystem, capability and binary
   checks pass, invoke from the reviewed source tree with absolute paths:

   ```sh
   env -i PATH=/usr/bin:/bin \
     /usr/bin/python3 scripts/monad_alpha_e2e.py \
       --authorize RUN-IN-APPROVED-DISPOSABLE-VM \
       --mpkg /absolute/path/to/reviewed/mpkg \
       --area /var/tmp/monad-alpha-release-test-<unique>
   ```

   Replace both path placeholders before use. This procedure is specific to
   the clone; the command was not executed during preparation.
4. Preserve terminal output and the complete area, including `results.json`,
   on success or failure. Do not rerun in the same area, clean it, or claim
   release qualification from the E2E alone. Reconcile and review clone
   evidence separately. The harness does not run privileged CTest cases.

## Verification record

Build directory: `/tmp/monad-alpha-release-test-20261031` (new isolated
Release directory; GNU C++ 14.2.0). Configure and full build completed.

The project nonprivileged CTest selection (`-LE privileged -E
'^artifact_scanner_test$'`) selected 147 tests: **143 PASS, 0 FAIL, 4 SKIP,
0 TIMEOUT**. The four skips were `object_store_scanner_test`,
`artifact_identity_scanner_test`, `transaction_gc_scanner_test`, and
`package_operation_scanner_test`; each reports unavailable xattr/capability
preconditions. `artifact_scanner_test` was explicitly excluded by the
project's bounded-TOML qualification convention (**1 NOT RUN**). The
privileged-labeled cases were excluded and remain **NOT RUN**. The qualification
driver and all three new guard tests passed. Total CTest time: 219.52 seconds.

The binary reports exactly `Monad Package Manager 0.1.0-alpha.1`. Dynamic
linkage resolves `libathena_core.so` from this build directory, and the core
library SONAME is `libathena_core.so`. The ELF dependencies include
`libcrypto.so.3` and `libarchive.so.13`; `ldd` resolved all listed dependencies
on this Debian guest.

SHA-256 values measured from the completed build (local measurements, not
security pins or attestations):

| Build artifact | SHA-256 |
|---|---|
| `mpkg` | `c9b06dcb9621e50175afab4cefc090313aab9b36665047b48f20a76b717c926f` |
| `libathena_core.so` | `2d23dfcabc06a965a068f716be9fda26b465226f675c23eab4328061271ca2be` |
| generated `CTestTestfile.cmake` | `05295a563cc042c8c71f58a9ea1b98cf0b7be82617a09c8d68d4c88871401084` |

Privileged CTest and the E2E harness are intentionally NOT RUN. No sudo, push,
pin regeneration, invented hash, or attestation was used.

## Readiness

Preparation readiness is distinct from release readiness. The privileged E2E
and required privileged qualification remain NOT RUN, and clone isolation is
not independently established by this host-side identity check. The release
is not qualified by this document.
