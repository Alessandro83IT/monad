# Internal stores

## Legacy store inspected at 5054389

`store.{hpp,cpp}` accepts an explicit store root or the `paths::store()` root
from PathLayout (default `/var/lib/athena/store`). It addresses packages by
`<name>-<version>`, strips the staged `usr` boundary, recursively copies regular
files, creates parents, and embeds `.athena/metadata.toml` and the path-only
`.athena/manifest.toml`. It neither moves the source nor finalizes permissions.
File copying can dereference symlinks; empty directories are omitted. Existing
entries with manifests fail installation; incomplete entries can be deleted and
rebuilt. Lookup checks package metadata/manifests, never artifact identity.
Filesystem/TOML errors propagate as runtime errors. Existing store tests cover
metadata persistence and legacy compatibility; installer, activation, generation,
removal and GC tests cover their respective legacy contracts.

GenerationEntry records package and store path. Activation uses the path-only
manifest to create filesystem symlinks, with rollback on failure. Removal changes
generation membership and leaves objects for historical references. GC scans
top-level real directories with `.athena/metadata.toml`, protects references from
all persisted generations, and does not recursively discover object namespaces.
The legacy store does not itself acquire locks. Core FileLock uses flock and
LockSet orders state/store-GC/build-input/repository locks; those helpers are not
an atomic no-replace primitive. Existing acquisition/build staging uses curl,
transport checksum verification, tar and ambient CMake/Autotools DESTDIR builds.
None of these operations is changed by B.5.

## B.5 content-addressed payload store

`object_store.{hpp,cpp}` is a separate internal Linux module, not an installed
public API. ObjectStore takes an explicit absolute store root, already created
by its administrator. It validates/creates only `objects`, `objects/v1`, and
`objects/v1/sha256` below that root, refusing symlink components. Existing
namespace directories are not chmodded; newly created namespace directories
receive 0755 regardless of the ordinary restrictive umask. It does not touch
unrelated entries.

`object_path(ArtifactId)` accepts exactly `sha256:` followed by 64 lowercase
hexadecimal digits and maps to `<store>/objects/v1/sha256/<full-digest>`.
It has no filesystem side effects and does not assert existence or verification.
There are no package, candidate, repository, source or build-input keys.

Publication proceeds as follows:

1. Validate the expected ID; reject a source that contains the publication
   namespace, so creating private work cannot mutate that source.
2. Scan the caller's private quiescent source with the unchanged v1 scanner,
   normalize and locally derive ArtifactId. Require equality with the claim.
3. Open/create the namespace with descriptor-relative no-follow operations.
4. Create an unpredictable exclusive mkdirat `.publish-*` container immediately below
   the SHA-256 directory, with mode 0700 and a private `payload` directory.
   This remains private after payload hardening and is on the destination
   filesystem even if the namespace itself is mounted separately.
5. Strictly validate the buffered normalized model again. Materialize its bytes
   with pinned no-follow parent descriptors, exclusive new files and literal
   symlinkat targets. No rereading/copying source locators or hard links occurs.
   All explicit/empty directories are preserved. Apply logical modes 0755 for
   directories and 0644/0755 for files; ignore original ownership/timestamps.
   No source metadata is copied. Inherited host metadata is not silently
   accepted: subsequent scanning rejects anything unsupported.
6. Remove all write bits: root/directories and executable files 0555, other
   files 0444. Symlinks retain Linux symlink modes. Fsync regular files and
   directories bottom-up, then the private container. Independently scan/hash the
   hardened private payload and require the expected identity and exact model
   equality with the verified source.
7. Rename that complete payload using Linux renameat2(RENAME_NOREPLACE) with
   pinned source/destination directory descriptors. There is no check-then-rename
   or replacing/portable fallback. Synchronize both publication parents after
   rename. Unsupported kernels/filesystems fail.
8. Independently scan/hash the authoritative destination, whether this publisher
   won or lost the rename. Require matching identity and exact normalized model
   equality with the source, also rejecting distinct models under a hypothetical
   digest collision. Synchronize a valid existing winner and its parent too. Clean only
   this invocation's private container, never source or destination.

Concurrent processes share no mutable staging state and need no process-local
mutex or flock for publication correctness. One wins; a loser verifies the
winner. Repeated publication and independent equivalent sources converge on
one path. EEXIST includes corrupt directories, files and dangling symlinks: none
is overwritten, deleted, chmodded or repaired by this API.

`verify(expected)` scans the derived authoritative path with no writes. An
inspectable mismatching payload is CollisionCorruption; incomplete inspection
is ExistingObjectVerification. Source mismatch is SourceIdentityMismatch;
invalid source inspection is InvalidSource. Separate codes cover malformed IDs,
namespace/private staging/materialization/rename failures, post-publication
integrity and cleanup. A post-rename inspection or mismatch failure reports
OUTCOME UNCERTAIN (with the integrity cause nested) and leaves the authoritative
object intact. Later
maintenance tooling must diagnose/quarantine/recover it explicitly; B.5 supplies
no repair tool. ObjectError retains secondary cleanup diagnostics in
`cleanup_failure()` while preserving its primary category/message. Unexpected
exceptions are preserved; cleanup never replaces the primary failure. M2.5-A
PersistenceError retains stage/errno, uncertainty and secondary cleanup detail.
`publish_durable` additionally distinguishes Published from verified AlreadyExists.

Private cleanup restores write/traversal permissions only inside its own
container and removes it without following payload links. A crash can leave a
`.publish-*` container; it is never authoritative, and B.5 does not sweep other
publishers' work. Those names cannot be valid full-digest object names.

### Exact guarantees and phase boundary

The API never mutates or replaces an already-published object. Read-only modes
prevent ordinary writes to files and edits within payload directories without
explicitly changing permissions. Read and traversal remain available for
verification and future activation/inspection. Physical modes normalize back
to the same schema semantics. This is administrative hardening, not protection
against root, the owning user deliberately chmodding data, or hostile writers
with access to the store namespace. No chattr immutable flag is set (v1 rejects
it). The store administrator must control namespace writers; source trees must
be quiescent, as required by the scanner. No root-proof or kernel-snapshot
claim is made. The source is never chmodded, chowned, moved, deleted or rewritten;
the scanner's documented possible kernel readlink atime accounting still applies.

M2.5-B adds durable immutable publication under the conservative M2.5-A
filesystem contract (ext family, XFS, Btrfs; tmpfs only for ordering tests).
Overlayfs remains unsupported. Failed barriers after visibility report OUTCOME
UNCERTAIN. SIGKILL testing does not qualify power-loss behavior; actual storage
qualification is M2.5-G. See [the M2.5-B audit](../../docs/m2.5-b-durable-publication.md)
for generation publication, lock expectations and the deferred legacy limitation.

No persisted manifest or realization metadata is needed for this primitive:
the full identity is in the namespace, and verification reconstructs the v1
model from the actual payload. No bookkeeping or ID record is added inside
the hashed tree. A payload `.athena` is ordinary payload. Human-readable
inspection manifests and candidate/provenance/generation associations remain
separate later integration work; legacy manifests are not reinterpreted.

Legacy objects coexist unchanged. Installer, executor, resolver, generations,
activation, removal and GC have no callers of ObjectStore in B.5. No GC runs
over this namespace. B.5 completes the low-level publication prerequisite;
M2.2-C can begin explicitly scoped realization/reference integration and dual
legacy/new readers. It does not complete M2.2 execution or implement BuildInputId.

### Verification coverage and scanner limitation

`object_store_test` tests pure mapping/rejection, strict/defensive materialization,
canonical and hardened modes, raw/binary bytes, empty trees/directories, literal
absolute/escaping links, hard-link absence, no overwrite and real process-level
atomic visibility/no-replace mechanics. Internal `object_store_detail.hpp`
exposes mechanics for these tests; it does not establish local verification and
is not an alternative authoritative API.

`object_store_scanner_test` uses the production publication/verification API for
source purity, first/empty publication, repeat/independent-source convergence,
source mismatch, legacy coexistence, corrupt preexisting destinations, five
tamper categories, cleanup after conflicts and two process concurrency cases
(same source and independent equivalent sources). An observing parent requires
every visible destination to be complete/finalized. Tests use mkdtemp roots and
checks active in Debug and Release. Scanner-dependent success returns CTest
skip code 77 only for the existing hidden trusted.* inspection limitation.
No scanner result is mocked and no incomplete metadata inspection succeeds.
Full publication/tampering/concurrency coverage must also run on a host with
sufficient initial-namespace CAP_SYS_ADMIN (or a supported filesystem that
explicitly cannot store xattrs). Mechanics passing cannot substitute for that
privileged integration coverage. Whole-tree buffering remains a resource limit.

## C.3 activation consumer

Generation switching now accepts an explicit const ObjectStore context and uses
object_path plus full verify before projecting realization-backed memberships.
Publication, mapping, verification, schema and legacy storage are unchanged.
No embedded administrative manifest is required or inserted. Runtime projection
reads the verified payload tree, including .athena as ordinary data, and creates
only target links/shared directories. Full verification rejects missing/tampered
objects and incomplete inspection rather than trusting hardening permissions.
The quiescent store/administrator assumption and readlink atime limitation remain.
See ../generation/README.md for mixed-source identity, rollback and API boundaries.
GC still does not traverse this namespace; later GC must retain ArtifactIds from
all retained generations, shared artifacts and separately defined runtime roots.

## M2.5-C lifetime and GC protocol

Production publishes prepared objects through `TransactionPins::publish`, which
establishes a durable pin under StoreGc before publication or adoption. Old B
publication seams are explicitly named `*_unprotected_for_test`. Authoritative
recursive removal has been removed; bound GC detaches to claimed same-mount trash
before private recursive reclamation. All valid pins, including unlocked stale
transactions, remain roots. See [the C contract](../../docs/m2.5-c-transaction-pins-gc.md)
for binding, validation, locking, failure and recovery boundaries.
