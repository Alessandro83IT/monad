# Realization record contract (M2.2-C.1)

RealizationRecord is an internal durable-state value connecting one exact
selected core::CandidateIdentity to one locally verified ArtifactId in the
configured authoritative ObjectStore. It is not a new identity layer or a
candidate/artifact lookup table. Multiple candidates may share an artifact;
one candidate may have multiple realized artifacts. No unique realization ID,
timestamp, package metadata copy, or global one-to-one mapping is introduced.

The fields are candidate, artifact_id, optional build_input_id, and origin.
CandidateIdentity is preserved in full, including its snapshot and repository
provenance. Its repository fields, optional revision and optional provenance
snapshot must agree; the selected snapshot must include its identity and
metadata digest, as required by the public InstallPlan boundary. Existing
package/version, snapshot and provenance validators are reused.

CandidateIdentity already owns the repository provenance required by ADR-001,
including the optional metadata locator. Persisting it in this record preserves
that association without needing a repository reselection or a durable plan.
No additional RepositoryProvenance or free-form artifact Provenance copy is
added. Current legacy installation state has no authoritative association for
these identities. This contract records repository selection and origin, not
signatures, producer attestations, full build audit evidence, source-content
verification evidence, dependency semantics or effective configuration. Future
generation state must retain the latter semantics separately where required.

ArtifactId must use the ObjectStore v1 full lowercase SHA-256 spelling.
BuildInputId, when present, must use ADR-001's same textual digest convention,
but remains a distinct typed identity. C.1 does not derive or independently
verify build inputs. BuiltFromAthenaIR requires a known BuildInputId;
AmbientSourceBuild and ImportedBinary require absence. Ambient CMake/Autotools
execution must never claim IR origin. Imported producer claims remain deferred.

The smallest stable store reference is ArtifactId interpreted in the v1 object
namespace. object_reference() derives `objects/v1/sha256/<full-digest>` relative
to the configured store root. No extra path or second artifact identity is
stored; absolute, private, temporary and contradictory object references are
unrepresentable. This matches ObjectStore::object_path without changing that
API. The root is deployment configuration, outside the record.

The required producer ordering is realization, local ArtifactId derivation,
successful ObjectStore::publish (or ObjectStore::verify for an existing object),
then construction of an authoritative association. Structural validation and
codec decoding do not prove an object exists, was verified, or remains intact.
This is a producer precondition, not an enforced verification token: C.1 has
no producer integration. ObjectStore remains responsible for filesystem
verification and publication. Its atomic visibility guarantee does not imply
power-loss durability; this record does not strengthen it.

The standalone deterministic TOML codec has an explicit schema_version = 1.
It preserves every CandidateIdentity field and optional presence exactly,
including the optional nested provenance snapshot. It stores ArtifactId once;
the canonical reference round-trips by derivation. Unknown versions, fields,
wrong types, malformed identities and contradictory associations are rejected.
No persistence destination or existing generation/state codec is changed.
Serialization validates first, uses TOML escaping and the classic locale, and
has no filesystem operations. Equality compares all fields; ordering extends
the existing complete candidate semantic key with artifact identity, optional
build identity and origin. No locale, pointer or filesystem lookup is involved.
These semantics are ready for deterministic generation serialization in C.2.

Legacy `<store>/<name>-<version>` objects and historical generation entries
remain untouched. No identities or realization records are fabricated for them.
C.1 introduces no generation membership/persistence/switching/rollback,
activation, GC, installer/executor/acquisition/backend, resolver, CLI or removal
integration. ADR-001, Artifact Schema v1, hashing and publication are unchanged.
