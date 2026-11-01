# Monad 0.1.0-alpha.1 Xattr Blocker Resolution Review

Review date: 2026-10-31  
Branch: `main`  
HEAD at start: `6daa4a0b9a67441e7eacfc2ac9a3a4ae8d4acbe1`  
Version: `0.1.0-alpha.1`

## 1. Root cause

No correction was made because the required safe no-capability relaxation could
not be justified from the existing scanner API or installation flow. The
scanner does not fail on an xattr listing syscall in this environment. For
directories and regular files it calls `flistxattr`; for symlinks it calls
`llistxattr` using a pinned `/proc/self/fd/<parent>/<name>` locator. Linux
returns an empty visible list, while hiding `trusted.*` from this unprivileged
caller. The subsequent `can_list_trusted_attributes()` capability and initial
user-namespace identity check is what rejects the observation.

The diagnostic path is the artifact-relative path being scanned (the root is
shown as `<root>` in the prior install report). The metadata is the full set of
extended attributes, including ACL, capability, security-label and trusted
namespaces, along with separately checked inode flags. The denial is Linux
kernel namespace visibility under the process credentials/capabilities; it is
not a sandbox syscall denial, filesystem `EOPNOTSUPP`, or application failure
of `flistxattr`/`llistxattr`. The application then deliberately fails closed.

The scanner requires complete metadata inspection because it models only plain
directories, regular files and symlinks and rejects metadata that can change
access, security, or filesystem behavior. Treating an empty unprivileged list
as proof that `trusted.*` is absent would silently classify unknown metadata as
verified. That is not appropriate for the general scanner, which is reused for
build staging, object publication and verification, generation validation,
and legacy adoption.

## 2. Security analysis and design decision

A scoped nonprivileged mode would need evidence that every inode in the tree
was created through a constrained extraction path that cannot import or retain
trusted metadata. Current scan entry points accept ordinary filesystem paths
or pinned directories and carry no such provenance. Build execution is ambient
and can move an existing inode into staging; same-user ownership does not prove
that a privileged actor never attached hidden metadata. A generic flag, a
visible-empty-list rule, or a staging-path heuristic would therefore weaken
verification without proving the hidden namespace empty.

The safe design remains fail closed until Monad has a verifiable staging
provenance boundary or can inspect the namespace on a supported filesystem
that explicitly rejects xattrs. No filesystem privilege was added, and no
metadata or archive safety checks were bypassed. There is no reduction in
scanner security guarantees because production behavior is unchanged.

## 3. Code changes and regression tests

No source or test changes were made. The implementation in
`src/artifact/scanner.cpp` retains its complete-metadata gate. This review adds
only this report. The requested regression matrix (files, directories,
symlinks, accessible/inaccessible xattrs, malformed metadata, failure handling,
containment, and no partial activation) was not extended because there is no
safe behavioral change to exercise. Existing scanner regressions were included
in the full nonprivileged suite below; scanner-dependent cases skipped on this
host remain explicitly identified.

## 4. Build results

Fresh isolated Release directory: `/tmp/monad-alpha-xattr-20261031`

Commands:

```sh
cmake -S . -B /tmp/monad-alpha-xattr-20261031 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/monad-alpha-xattr-20261031 --parallel 4
/tmp/monad-alpha-xattr-20261031/mpkg --version
```

Configure and Release build completed successfully (287 build steps). The CLI
reported `Monad Package Manager 0.1.0-alpha.1`. Toolchain detection included
GNU C++ 14.2.0, CMake 3.31.6, OpenSSL Crypto 3.5.7, and LibArchive 3.7.4.

## 5. Full nonprivileged CTest results

Command:

```sh
ctest --test-dir /tmp/monad-alpha-xattr-20261031 --output-on-failure -LE privileged --parallel 4
```

Result: **140 PASS, 0 FAIL, 4 SKIP**. The skipped tests were
`object_store_scanner_test`, `artifact_identity_scanner_test`,
`transaction_gc_scanner_test`, and `package_operation_scanner_test`; each
depends on complete trusted-xattr inspection unavailable on this host. **25
privileged tests were NOT RUN** because the `privileged` label was excluded.
CTest reported 100% of selected tests passed; skipped cases are not counted as
passes.

## 6. Install/remove evidence

No new end-to-end run was recorded in this review. The immediately preceding
blocker report, [monad-0.1-alpha-blocker-resolution.md](monad-0.1-alpha-blocker-resolution.md),
records the fresh local package fixture and exact CLI command forms. Its
installation exited 1 at the hidden `trusted.*` check; info and remove also
exited 1 because the package was not installed. The active generation remained
generation 0. Therefore install, file verification, store integrity, current
generation after install, info, remove, post-removal consistency, switch, and
rollback are **NOT PASS**. No complete install/remove sequence has been proven.

## 7. Remaining limitations

- Successful unprivileged installation remains blocked on filesystems where
  xattrs are supported but the caller cannot inspect hidden `trusted.*` names.
- Scanner-dependent regression checks remain skipped on this host.
- The install/remove, switch and rollback flow remains unverified end to end.
- No release archive, checksum, manifest or release notes bundle was created;
  release preparation is gated on a successful install/remove demonstration.
- Alpha M1 qualification is separate and was not run or changed.

## 8. Release artifacts and SHA-256

No release artifacts were produced. There are no release artifact checksums.
The isolated build directory is verification output, not a source release.

## 9. Release gate

| Gate | Result |
|---|---|
| Release build | PASS |
| CLI version | PASS |
| Full nonprivileged CTest | PASS with 4 documented SKIP; 0 FAIL |
| Privileged tests | NOT RUN (25) |
| End-to-end install/remove | FAIL / NOT VERIFIED; install is known to stop at xattr gate |
| No writes outside test root | NOT REVERIFIED in this review |
| Release artifacts and archive inspection | NOT RUN; gate did not pass |

The release gate does not pass because the required installation sequence is
not complete and a safe implementation correction has not been established.

## 10. Final verdict

**`RC_BLOCKED_BY_ENVIRONMENT`**. The observed inability to inspect hidden
`trusted.*` metadata is an environment capability limitation, while removing
the gate with current provenance APIs would create a security defect. This
review preserves the fail-closed scanner. A future resolution needs either a
supported filesystem that explicitly reports xattrs unsupported or a reviewed
design that proves restricted staging provenance without trusting caller
claims. No sudo, root operation, privileged test, system-wide installation,
publication, commit, tag, or push was performed.
