# Generation membership, publication and activation (M2.5-B)

M2.5-D5 makes `discard` consult persisted activation plan references while holding
exclusive State. Published plans and complete private preparations retain their
target and selected-base generations. Ambiguous or invalid retention state blocks
discard conservatively, including for an otherwise unreferenced generation.
This does not change current selection or authorize plan/residue cleanup.
See [the D5 contract](../../docs/m2.5-d5-plan-retention.md).

A Generation retains its independently allocated numeric ID (max retained ID + 1).
It is immutable once persisted. Generation identity is neither CandidateIdentity,
ArtifactId nor BuildInputId. Membership does not derive the generation ID.

## Representation and compatibility

GenerationEntry retains its legacy package/store_path fields and optional captured restricted semantics.
Generation.entries contains only these values. Generation.realizations contains
only C.1 RealizationRecord values, preserving the complete selected candidate,
repository snapshot/provenance, ArtifactId, optional BuildInputId, origin and installed semantics.
These distinct typed collections avoid optional identity fields and preserve
source compatibility for the existing installer, activation, removal and GC
consumers, which access legacy fields directly. This is the concrete reason
for partitioning the in-memory model rather than replacing GenerationEntry
with a variant and migrating all those consumers in C.2. There is no competing
realization structure. The semantic package metadata lives within its record;
candidate repository provenance remains in CandidateIdentity.

The durable schema uses explicit tagged variants:

```toml
schema_version = 3
id = 12
policies = []

[[memberships]]
kind = 'legacy'
package = 'example'
store = '/athena/store/example-1.0'

[[memberships]]
kind = 'realization'
[memberships.record]
# Standalone RealizationRecord schema_version = 2 with semantic state here.
```

The record table is encoded/decoded by the existing C.1 codec, including its
nested candidate tables. Empty generations emit memberships = []. All newly
written generations use version 3, including legacy-only state, with a policies array. Existing
unversioned id + [[entries]] documents are read as legacy without inventing
identities, rewriting files, scanning objects or automatically migrating them.
There was no explicit generation version 1. Explicit generation versions 2 and 3 are supported; realization versions 1 and 2
remain independently validated. Version 3 legacy members may contain captured
semantic state. [Installed semantics](../installed/README.md) defines migration and
offline compatibility; decoding never invents missing old information.
Unknown fields, incorrect types, missing tags, unknown kinds, and cross-kind
fields are rejected. Historical documents may omit entries for an empty state.
A present malformed entries field is rejected rather than silently made empty.
Generation IDs must be nonnegative and fit TOML's signed 64-bit integer range
(`0` through `INT64_MAX`). Numeric directory names must be canonical decimal:
`0` or digits without a leading zero. Signed names, leading-zero aliases and
nonnumeric auxiliary directories are ignored; symlink entries are not roots.
Canonical numeric directories above `INT64_MAX` fail closed during enumeration
and allocation. Allocation after `INT64_MAX` fails without publishing a new ID.
There is no persisted lifetime allocation counter: discarding the highest retained
generation can permit that ID to be allocated again, as before this phase.

Legacy package/store values preserve historical string semantics, including
relative paths, empty strings and duplicate packages, which the old reader did
not prohibit. Missing or incorrectly typed fields are rejected. ArtifactId
spellings and paths containing the objects/v1/sha256 namespace (after lexical
normalization) cannot masquerade as legacy references. No payload access is
required. Legacy entries never acquire synthesized candidate/provenance or
artifact/build identities; a name/version locator provides none of that evidence.

RealizationRecord validates every new candidate, digest and origin association.
Generation validation additionally disallows duplicate realization package names
and overlap with legacy package names. The existing builder replaces by package
name, so simultaneous selections of two candidates/versions/artifacts for one
name are rejected, even if identical. This restriction applies to new realization
members and does not retroactively reject historical duplicate legacy entries.
Different package names/candidates may share the exact same ArtifactId and remain
distinguishable. C.1's ability to record multiple observations for one candidate
is unchanged; a generation selects only one observation per package name.

## Ordering, state API and persistence

Legacy order is preserved: the existing builder explicitly preserves entries,
replaces in place and appends new package names; existing tests assert this order.
Realization order is nonsemantic and canonicalized with RealizationRecord's
complete semantic comparator. Serialization emits legacy members in their order,
then sorted realization members, with TOML escaping and the classic locale.
Realization construction order, unordered containers, filesystem enumeration,
pointers and locale do not affect serialized state. Decoding canonicalizes only
the nonsemantic realization order and never writes the source document.

make_generation(id, legacy, realizations) is a pure validated construction API.
Generation::validate, serialize and deserialize_generation perform no filesystem
I/O. GenerationManager::create(legacy, realizations) allocates the independent ID
and persists metadata without activation. get/list/current use the dual reader;
list remains sorted by generation ID.

M2.5-B binds the configured authority to an absolute path once at construction.
Both constructors accept ordinary `./` components and trailing slashes; only
those components are removed. Parent traversal (`..`) is rejected, never
lexically collapsed across a potential symlink. Actual directory traversal
opens every component without following links. Later cwd changes cannot redirect
the manager's reads, allocation, publication or State-lock authority.

initialize/create prepare the durable namespace using the M2.5-A filesystem
preflight, safe directory creation and parent barriers. They then acquire an
exclusive namespace-derived State lock covering allocation and publication.
The fully validated serialized document is written with checked complete writes
to an exclusive `generation.toml` in a random private directory. Publication
orders file fsync, private-directory fsync, same-parent
`renameat2(RENAME_NOREPLACE)` to the final numeric name, and generation-parent
fsync. Collisions never overwrite. A failure after successful rename reports
OUTCOME UNCERTAIN and cleanup never removes the final generation. Callers must
not already hold an authority lock when invoking initialize/create.

Read-only enumeration opens existing directories without mutation, fsync or
durable-filesystem preflight. A missing namespace returns an empty list without
creating it. Unsafe authority traversal, inspection failures, out-of-range
canonical numeric directories and malformed metadata in a numeric directory
propagate errors. get/current also inspect existing metadata without initializing
the namespace and reject symlink traversal. GC root discovery uses this reader;
it does not initialize durable state merely to enumerate roots.

Publication does **not** select current. Every published numeric generation,
including an unselected one, is visible through list/get, protects its legacy
stores and ArtifactIds as GC roots, and participates in max-ID allocation.
CLI `generation list` displays it when current is readable; that command still
fails if the namespace exists but current is missing or corrupt. The list/get
API itself does not require current. Installed-package `list` and `info` inspect
only current, so unselected memberships do not appear as installed packages.
It is a switch candidate subject to the existing membership/integrity checks.
CLI rollback chooses the highest retained ID below current, regardless of whether
that ID was ever selected; an unselected ID above current is not the immediate
rollback choice but can become one after a larger ID is selected. There is no
persisted selected-history or separate commit marker. initialize separately
selects generation 0, and switch separately writes the unchanged textual current
pointer after activation. Neither operation makes that pointer crash-durable.
Publication versus transaction commitment and interrupted initialization recovery
remain M2.5-E/F integration concerns, not guarantees of this phase.

The immutable-publication ordering depends on M2.5-A's supported local filesystem,
durable-ancestor and honest-storage assumptions. tmpfs and SIGKILL tests prove
ordering and process-death behavior, not power-loss durability. Crash recovery
and actual storage/power-loss qualification remain later M2.5 phases.

ArtifactId is a typed content identity, never an arbitrary path. The immutable
object locator is derived by RealizationRecord::object_reference() relative to
the configured store root; no extra path is persisted. Structural validation
is not proof of object existence, verification or continuing integrity. Future
producers must publish/verify through ObjectStore before asserting the record's
authoritative association, as required by C.1.

Durable generation publication guarantees its metadata, not the durability of
an arbitrary referenced payload. It does not upgrade legacy `store::install`,
whose entry creation and metadata writes can still expose partial nondurable
state. The modern realization path publishes through ObjectStore and records
ArtifactId memberships; its artifact guarantee does not depend on that legacy
publication path. Legacy and mixed generations retain their existing reference
semantics without claiming new legacy payload durability.

## Phase boundaries

C.3 supports legacy, realization and mixed transitions. GenerationManager still
owns only state paths. The existing two-argument switch_to(id, target_root) and
both constructors remain source compatible for legacy callers. The new
switch_to(id, target_root, const ObjectStore&) takes an explicit authoritative
store context for transitions involving realizations, including switching away
from one. There is no default/hard-coded root, inferred state-directory sibling,
or global mutable configuration. Callers can construct ObjectStore from their
configured PathLayout.store. CLI install/remove/switch use the explicit ObjectStore context. Legacy-only callers
without that context fail before changes if either state has realizations.

Each realization resolves exclusively through ObjectStore::object_path(ArtifactId).
This is equivalent to configured root + RealizationRecord::object_reference(),
without storing another locator. Full ObjectStore::verify runs once per distinct
artifact in the union of previous and target states before target changes, even
for unchanged memberships and same-generation requests. Existence alone and
read-only modes do not establish continuing integrity against privileged or
owner tampering. Missing objects, corrupt content, symlinked namespaces/roots,
and incomplete inspection fail; there is no legacy fallback or repair. A second
scanner pass supplies the projection and its ArtifactId is checked again. The
existing quiescent, administrator-controlled namespace assumption still applies;
this is not a hostile-writer snapshot or authenticity guarantee. Inability to
inspect hidden trusted.* xattrs remains a hard verification failure.

ActivationSource is an internal runtime value, never persisted. Its semantic key
is (legacy, exact historical store-path string) or (object, full ArtifactId) within
the one explicit ObjectStore context. Candidate, package, origin and BuildInputId
are generation metadata, not filesystem-source identity. Different candidates
sharing one artifact keep distinct metadata but activate once; changing only
candidate metadata leaves existing links untouched. Changing ArtifactId removes
the old projection and adds the new one. Namespace tags never infer equivalence
from equal/resolved locators; legacy entries never acquire fabricated identities.
C.2's rejection of object-namespace paths as legacy references remains unchanged.

The low-level activation API receives source directories and a runtime Projection
of relative link/directory paths, without ArtifactIds. Both manifest and object
projections use the same link creation/ownership checks and activation rollback.
Legacy-only transitions retain historical manifest/path behavior. Mixed transitions
read legacy manifests and use the directory-aware projection with the same strict
target-ancestor checks as objects. Real target directories merge; files and source
symlinks project as symlinks to the exact source entry. Literal source link targets
are never canonicalized, copied or rewritten; .athena is ordinary object payload.
Explicit and empty directories become real shared target directories. As with
legacy parent directories, scaffolding remains after deactivation, including empty
directories; C.3 does not introduce directory ownership/removal. Consequently a
retained directory can conflict with a later file projection at that path.
Target symlink ancestors and targets inside a source or the authoritative object
namespace are rejected in object/mixed
projections, so activation cannot write through a projected link into payloads.
No source contents, modes, ownership, mtime or ctime are changed. Scanner readlink
atime accounting retains its documented kernel limitation. Directory modes in
the target are ordinary activation scaffolding, not immutable payload modes.

Switching removes old-only sources, adds new-only sources and writes current only
after successful filesystem operations. Conflicts never overwrite target objects.
A failed source activation undoes its created links and explicit directories;
a later switch failure deactivates earlier successful additions, removes empty
directories absent before additions (including a failed addition), then restores
removed sources in reverse order, preserving the original exception. Restoration
and cleanup remain best effort under independent filesystem errors; this is not
an atomic filesystem snapshot, crash recovery or a new power-loss durability
contract. Shared target scaffolding can remain after rollback as in legacy
activation. Strict deactivation validates link ownership before removal. Existing
current-pointer persistence semantics are unchanged.

GC reads both membership collections from every retained generation. Realization
ArtifactIds remain its roots regardless of semantic metadata; it does not re-resolve
dependencies or hash records into artifact identities.

M2.4 persists installed semantics with realization records and independent system
requirements on generations. Install validates preserved plus planned membership;
remove validates surviving membership. Generation switching validates targets
for all schema-3 input before filesystem changes, including unknown semantic
records. Decoded pre-M2.4 input retains an explicit historical compatibility mode.
Historical pre-M2.4 generations
remain readable/switchable under their original integrity contract; preserving
unknown realization dependency state in a new semantic operation fails explicitly.
See [installed semantics](../installed/README.md) for the legacy compatibility policy.
M2.5-B adds the immutable publication and allocation lock described above.
It does not add whole-transaction locking, durable current selection, activation
reconciliation or startup crash recovery.

## C.3 verification coverage

`generation_activation_test` publishes actual objects through ObjectStore and
covers legacy/object transitions, identical sources and candidate-only changes,
shared artifacts, artifact replacement, mixed generations, missing/corrupt
objects, conflicts, rollback, pointer ordering, immutability, payload types and
legacy aliases. It never substitutes mechanical materialization for verified
publication. The missing-object check runs before publication probing. Return
code 77 skips subsequent authoritative integration only for the existing hidden
trusted.* inspection limitation; that coverage must run on an initial-namespace
CAP_SYS_ADMIN host or a supported filesystem explicitly rejecting xattrs.

`activation_projection_test` independently exercises the shared filesystem
projection on hardened trees without claiming ObjectStore authority: binary and
executable files, literal symlinks, empty/shared directories, conflicts, ancestor
and source-overlap rejection, immutability and rollback. It also exercises the
central switch transaction's multiple-addition and new-directory rollback with
legacy manifests. Checks in both new tests remain active in Debug and Release.
Existing activation/generation/installer/removal/GC/ObjectStore tests are retained;
C.2 guard checks now assert the explicit missing-context error.

M2.5-C adds `create_pinned` for protected realization membership publication under
State then StoreGc. Ordinary `create` rejects unchecked realization records;
`create_unprotected_for_test` is the explicit metadata-only fixture seam. Discard
uses exclusive State; switch retains shared State while it uses persisted roots.
All persisted generations remain roots, so pin handoff does not require current
selection. Internal authority-binding files do not change generation/current
formats or historical decoding.

### Historical current metadata publication

`GenerationManager::write_current` publishes the decimal record using an exclusive
0600 temporary in the generation directory, a complete write and file barrier,
atomic rename, then a directory barrier. The mode is safe with umask 0002, 0022
and 0077 without changing the process umask. Existing nonregular, foreign-owned,
multiply linked, special-mode or group/world-writable current entries are rejected;
they are never chmodded or replaced as a repair. The authority directory must be
trusted against concurrent untrusted entry mutation, as required by the filesystem
primitive. This does not extend the historical switch into a crash-recoverable
payload transaction. If publication succeeds but its directory barrier fails,
selection is uncertain and the historical switch propagates that error without
rolling payloads back against a possibly published selection.

The general generation reader still accepts the bounded historical decimal and
CurrentV1 formats; the D6 selection and D3 adoption readers retain their stricter
ownership, link, mode, mount and identity checks. No reader checks are relaxed.
