# Monad 0.1 Alpha M3 — archive extraction and store integrity

**Decision: NO-GO. Alpha M1 remains BLOCKED.** M3 closes some local source-level
risks in legacy tar extraction and store publication. It does not qualify the
release, change the approved candidate, or replace the Debian VM campaign.

## Scope and preserved state

- Date: 2026-10-27. Branch `main`, starting `HEAD`
  `abd6bac4b368427b76f6c98522afde928b418939`.
- Before edits, the worktree already had extensive modified source and test
  files, many untracked candidate/evidence files, and populated build
  directories. They were treated as user work. Changes in M3 are an overlay on
  those files; no pre-existing edits, evidence, or build directories were
  reset, cleaned, or deleted.
- M1 and M2 reports were read in full:
  `monad-0.1-alpha-m1-package-audit-20261027.md` and
  `monad-0.1-alpha-m2-stabilization-20261027.md`.
- Compilation output is in the new isolated directory `/tmp/monad-m3-build`.
  No install, network acquisition, privileged test, collector, host driver,
  commit, push, baseline edit, pin edit, or credential access occurred.

## Trust boundaries and existing flow

1. Downloaded archive bytes are untrusted. The downloader writes an archive
   under the build/cache tree; the earlier M1 work replaced shell command
   construction with direct argv execution.
2. Legacy source extraction previously invoked external `tar` into a fresh
   directory, then accepted the sole top-level directory. M2 made the
   extraction directory unique and stopped deleting a name derived from the
   archive. Tar remained responsible for interpreting entry names and links.
3. The build backend runs package-provided build logic with the invoking user's
   authority and writes a DESTDIR-style tree under `staging/usr`. This remains
   a separate untrusted-code boundary and is not sandboxed by M3.
4. Legacy `store::install` copied that tree into `store/<name>-<version>`, then
   wrote metadata and manifest. Before M3, it deleted an existing incomplete
   destination and copied directly into the final path. Immutable object-store
   and generation publication use separate protocols and are not evidence
   about this legacy API.
5. Installation/activation recovery and rollback live in transaction,
   generation, and activation code. M3 does not alter administrative
   activation/recovery paths or make an installation transaction.

Filesystem-changing operations in the changed path are now limited to creating
a private extraction directory, creating extracted regular files/directories,
creating one private sibling store staging directory, copying verified regular
file descriptors into it, writing metadata/manifest there, atomically
publishing it, and cleaning the operation-owned temporary tree after an
exception. Existing final store destinations are not deleted or overwritten.

## Changes

### Archive extraction

`src/archive/archive.cpp` now reads gzip tar through LibArchive instead of
delegating archive interpretation to a child `tar` process. It creates a
unique mode-0700 directory in the requested destination and removes only that
directory on failure. The extractor accepts regular files and directories;
rejects absolute paths, `..`, symlinks, hardlinks, special files, setuid/setgid
bits, duplicate normalized names, file/directory collisions, multiple roots,
corrupt/truncated data, entry counts above 100,000, entries above 2 GiB, and
total expanded file bytes above 8 GiB. File modes are reduced to ordinary read,
write, and execute bits; archive ownership and special permission metadata are
not applied. Existing `.` path components are normalized, while `..` is
rejected before normalization.

The private extraction directory prevents other users from replacing
intermediate path components. Same-UID hostile concurrent modification is not
a supported isolation boundary. Resource caps are fixed constants in this
implementation and may need tuning for supported source packages.

### Legacy store publication

`src/store/store.cpp` now validates staging before creating store output,
rejects symlinks and special entries, and copies file bytes using descriptor
operations with `O_NOFOLLOW` at every path component. It creates a mode-0700
sibling staging directory on the store filesystem, writes package files and
metadata/manifest there, then publishes with Linux `renameat2(RENAME_NOREPLACE)`.
The no-replace operation closes the final existence-check race. If the target
already exists, including an incomplete or dangling-link collision, it is
preserved and publication fails closed. Exception cleanup removes only the
private staging directory created by that invocation.

This provides atomic visibility of a complete directory on the same
filesystem; it does not add fsync-based power-loss durability. A process crash
can leave a `.monad-store-stage-*` directory. Automatic stale-stage cleanup is
intentionally absent because after a crash ownership/liveness cannot safely be
proven; recovery is conservative and manual. A broader transaction recovery
test after a mid-copy fault is still needed.

### Regression coverage

- New `tests/archive_security_test.cpp`: safe extraction; traversal, symlink,
  special-file, duplicate-entry, and truncated-archive rejection; confirms a
  traversal target is not created.
- Extended `tests/store_metadata_test.cpp`: existing destination sentinel is
  preserved after collision; an `RLIMIT_FSIZE` failure during private-stage
  copying leaves no final destination or stage; existing symlink-root/
  symlink-entry and manifest path cases remain covered.
- Updated `tests/process_invocation_test.cpp` to use a real fixture archive
  with an unusual filename while retaining its direct-argv downloader shell
  injection regression.
- `CMakeLists.txt` registers the new archive test and links its fixture writer
  to LibArchive.

## Verification

Fresh Debug configure in `/tmp/monad-m3-build` — **PASS** (GCC 14.2.0,
LibArchive 3.7.4). The core and selected test targets built successfully.

| Check | Result | Evidence |
|---|---|---|
| `process_invocation_test` | **PASS** | Direct-argv/download regression and unusual archive filename extraction |
| `archive_security_test` | **PASS** | Synthetic malicious/corrupt tar fixtures listed above |
| `store_metadata_test` | **PASS** | Legacy store metadata, symlink rejection, existing-destination preservation and stage cleanup |
| `installer_e2e_test` | **PASS** | Nonprivileged local installer fixture |
| `activation_test` | **PASS** | Nonprivileged activation unit/state tests |
| `generation_test` | **PASS** | Nonprivileged generation tests |
| `git diff --check` | **PASS** | No whitespace errors |
| Broad CTest suite | **NOT RUN** | Kept to targeted scope; M1/M2 inventory and scanner issues remain open |
| Mid-copy injected failure | **PASS** | `store_metadata_test` imposes a temporary `RLIMIT_FSIZE`, verifies write failure, no final destination, and cleanup of the operation-owned private stage |
| Process-crash recovery of an abandoned stage | **NOT RUN** | Crash-left stage cleanup is deliberately conservative and requires a separate recovery design |
| Privileged, scanner-capability, diagnostic, Debian VM cases | **NOT RUN** | Explicitly excluded by M3 constraints; no privileged case was invoked |
| Alpha M1 collector / Session 68 host driver / installation | **NOT RUN** | Explicitly prohibited by M3 scope |

The final focused CTest invocation selected six tests and reported **6 PASS,
0 FAIL, 0 SKIP, 0 TIMEOUT**. No status is inferred for any unselected test.

## Vulnerability status and residual issues

| Area | M3 status | Remaining evidence or limitation |
|---|---|---|
| Tar path traversal and absolute names | **Addressed locally** | Synthetic traversal case passes; supported archive corpus not broadly exercised |
| Symlink/hardlink escape and special files | **Addressed locally** | Entries rejected before extraction; package source compatibility needs broader fixture coverage |
| Duplicate paths and file/directory collisions | **Addressed locally** | Exact duplicate rejected; collision logic is source-reviewed, but no dedicated collision fixture yet |
| Truncated/corrupt input | **Partially addressed** | Truncated fixture rejects; exhaustive malformed compression/parser fuzzing NOT RUN |
| Archive resource exhaustion | **Partially addressed** | Entry and expanded-byte limits exist; parser CPU/memory and sparse/extreme metadata behavior are not exhaustively bounded |
| Extraction path races | **Improved** | Private mode-0700 root and no link extraction; same-UID concurrent attacker remains outside boundary |
| Legacy store partial publication | **Addressed for atomic visibility** | Staged sibling and atomic no-replace rename; power-loss durability and crash recovery of stale stages remain open |
| Existing store destinations | **Addressed conservatively** | Never replaced or removed; incomplete legacy entries now require explicit recovery outside this function |
| Store source race | **Improved** | Descriptor-relative no-follow opens protect each copied file path; directory enumeration is not a snapshot against same-UID concurrent changes |
| Cleanup ownership | **Addressed for caught failures** | RAII cleanup owns only the newly created private stage; crash-left stages are preserved |
| Resource use while copying legacy staging | **OPEN** | No total file-count/byte cap for arbitrary staged trees |
| Build script trust | **OPEN** | Build scripts still execute with caller authority |

## Alpha M1 qualification impact

M3 resolves the M2 report's specific external-tar interpretation gap for the
legacy extractor and the legacy store's direct-to-final copy / deletion of an
incomplete collision. It adds local regressions for those boundaries. These
are source-level changes and do not close the separate qualification gates.

| Alpha M1 matrix blocker from M1/M2 | M3 result |
|---|---|
| Current source closure / official input inventory mismatch | **OPEN** — no inventory or baseline changed |
| Reviewed candidate build identity and security pins | **OPEN** — no pin promotion occurred |
| Production scanner physical proof and privileged integration cases | **OPEN / NOT RUN** |
| Approved Debian VM campaign and evidence review | **OPEN / NOT RUN** |
| Legacy manifest schema strictness | **OPEN** |
| Build isolation/trust model | **OPEN** |
| Legacy extraction and store publication source risks described in M2 | **PARTIALLY RESOLVED LOCALLY** — see residuals above |

**GO/NO-GO: NO-GO. Alpha M1 remains BLOCKED.** No release, promotion, or
qualification claim follows from this M3 report.

## Proposed macrosession M4

1. Review M3 source changes against the frozen candidate input inventory; do
   not rewrite historical inventory or pins in place.
2. Add deterministic file/directory collision fixtures, byte/entry-limit
   boundary tests, and package archive compatibility fixtures.
3. Define and test conservative recovery for crash-left private store stages,
   including ownership evidence and no-follow cleanup semantics; decide whether
   fsync durability is required by the legacy API contract.
4. Add explicit file-count/byte budgets to legacy staging copy and test them.
5. Run a separately inventoried nonprivileged suite after owner review of all
   source inputs. Preserve the known qualification-driver mismatch until its
   official promotion workflow is reviewed.
6. Keep the approved Debian VM privileged campaign and pin promotion under
   their existing independent approval and evidence-review processes. Until
   both gates close, retain BLOCKED/NO-GO.
