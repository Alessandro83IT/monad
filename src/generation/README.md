# Generation membership (M2.2-C.2)

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

Legacy switch/rollback behavior remains operational. switch_to loads both states
and rejects any transition involving realization membership before activation,
deactivation or current-pointer changes. New states can be constructed, persisted
and selected for inspection by ID (get); making them active is not implemented.
No pointer-only selection API bypasses that boundary. The legacy activation
interface reads embedded path-only manifests and is unchanged; there are no
ArtifactId profile links, new conflict rules or profile layout changes.

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
presentation and consumers that only read entries remain legacy-only. Since new
states cannot become active through this API, install/removal do not encounter
a newly activated realization state. Manually editing current is unsupported.
ADR-001, Artifact Schema v1, identities, publication and C.1 are unchanged.
