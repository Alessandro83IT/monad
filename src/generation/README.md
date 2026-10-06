# Generation membership and activation (M2.2-C.3)

A Generation retains its independent monotonically allocated numeric ID.
It is immutable once persisted. Generation identity is neither CandidateIdentity,
ArtifactId nor BuildInputId. Membership does not derive the generation ID.

## Representation and compatibility

GenerationEntry remains an explicitly legacy package string plus store_path.
Generation.entries contains only these values. Generation.realizations contains
only C.1 RealizationRecord values, preserving the complete selected candidate,
repository snapshot/provenance, ArtifactId, optional BuildInputId and origin.
These distinct typed collections avoid optional identity fields and preserve
source compatibility for the existing installer, activation, removal and GC
consumers, which access legacy fields directly. This is the concrete reason
for partitioning the in-memory model rather than replacing GenerationEntry
with a variant and migrating all those consumers in C.2. There is no competing
realization structure or duplicated package/provenance copy.

The durable schema uses explicit tagged variants:

```toml
schema_version = 2
id = 12

[[memberships]]
kind = 'legacy'
package = 'example'
store = '/athena/store/example-1.0'

[[memberships]]
kind = 'realization'
[memberships.record]
# Complete standalone RealizationRecord schema_version = 1 envelope here.
```

The record table is encoded/decoded by the existing C.1 codec, including its
nested candidate tables. Empty generations emit memberships = []. All newly
written generations use version 2, including legacy-only state. Existing
unversioned id + [[entries]] documents are read as legacy without inventing
identities, rewriting files, scanning objects or automatically migrating them.
There was no explicit generation version 1. Explicit versions other than integer
2 are rejected; the nested realization version remains independently 1.
Unknown fields, incorrect types, missing tags, unknown kinds, and cross-kind
fields are rejected. Historical documents may omit entries for an empty state.
A present malformed entries field is rejected rather than silently made empty.
Generation IDs must be nonnegative and fit TOML's signed 64-bit integer range.

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
list remains sorted by generation ID. Serialization validates before creation
of the generation directory; output close failures are reported. Persistence
retains the existing directory/no-overwrite approach; C.2 adds no atomic-write,
fsync or power-loss durability guarantee.

ArtifactId is a typed content identity, never an arbitrary path. The immutable
object locator is derived by RealizationRecord::object_reference() relative to
the configured store root; no extra path is persisted. Structural validation
is not proof of object existence, verification or continuing integrity. Future
producers must publish/verify through ObjectStore before asserting the record's
authoritative association, as required by C.1.

## Phase boundaries

C.3 supports legacy, realization and mixed transitions. GenerationManager still
owns only state paths. The existing two-argument switch_to(id, target_root) and
both constructors remain source compatible for legacy callers. The new
switch_to(id, target_root, const ObjectStore&) takes an explicit authoritative
store context for transitions involving realizations, including switching away
from one. There is no default/hard-coded root, inferred state-directory sibling,
or global mutable configuration. Callers can construct ObjectStore from their
configured PathLayout.store. Installer/CLI callers remain on the legacy API;
without explicit context they fail before changes if either state has realizations.

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

GC is unchanged. It reads all generations' legacy entries as historical roots,
then scans top-level legacy objects with embedded metadata; it does not traverse
the ObjectStore namespace. Future GC must traverse realization members from all
retained generations and use their ArtifactIds/derived references without payload
rescanning, preserving shared objects and any separately specified runtime
references. C.2 implements no such traversal or collection.

Installer/executor, CLI, removal, installed-environment reconstruction and legacy
objects are unchanged. No executor emits realization members yet. Future flow is
InstallPlan selection -> realization -> verified ObjectStore publication ->
RealizationRecord -> generation membership. Dependency/effective configuration
semantics needed by future removal and runtime reachability are not supplied by
RealizationRecord; those remain later integration requirements. CLI membership
presentation and consumers that only read entries remain legacy-only. Realization states can now be activated through the explicit internal switch
context. Legacy-only install/removal/CLI consumers are not migrated and must not
be used to manage such states; their switch calls lack the required context and
reject realization transitions. Installed-environment reconstruction and removal
planning still require future integration. Manually editing current is unsupported.
ADR-001, Artifact Schema v1, identities, publication and C.1 are unchanged.

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
