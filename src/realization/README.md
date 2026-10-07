# Realization record contract (M2.4)

RealizationRecord is an internal durable-state value connecting one exact
selected core::CandidateIdentity to one locally verified ArtifactId in the
configured authoritative ObjectStore. It is not a new identity layer or a
candidate/artifact lookup table. Multiple candidates may share an artifact;
one candidate may have multiple realized artifacts. No unique realization ID,
timestamp or global one-to-one mapping is introduced. Installed semantics retain
the package metadata needed for offline environment validation.

The fields are candidate, artifact_id, optional build_input_id, origin, and optional
installed semantics. New executor records always include semantics; absence is
reserved for historical v1 records. [Installed semantic state](../installed/README.md)
describes the reused dependency model and conservative compatibility rules.
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
verification evidence or full build audit configuration. M2.4 records installed
dependency semantics, effective role/feature policy and resolution environments
separately from the shared payload.

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

The standalone deterministic TOML codec writes schema_version = 2 for records
with semantics, and preserves schema_version = 1 for historical records without it.
Version 2 requires the independently versioned semantic table.
It preserves every CandidateIdentity field and optional presence exactly,
including the optional nested provenance snapshot. It stores ArtifactId once;
the canonical reference round-trips by derivation. Unknown versions, fields,
wrong types, malformed identities and contradictory associations are rejected.
No persistence destination is implied by the record codec. Generation v3 embeds
these records and independent policy requirements.
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

C.3 now consumes these associations through generation switching with an explicit
ObjectStore context. Resolution uses ObjectStore::object_path(artifact_id), and
full verification rejects missing or corrupt objects before target changes.
Record structure, canonical relative reference, producer preconditions and codec
remain unchanged; activation supplies no producer or BuildInputId integration.

C.4 adds `realize(const core::InstallPlan&, const store::ObjectStore&,
const ExecutionOptions&)` in executor.hpp. It preserves the canonical
`selections()` order and does not mutate the plan. workspace_root is an explicit,
existing absolute executor-managed directory without hostile writers. Each
selection receives a private mkdtemp directory; package identity never forms
filesystem paths. Shell-sensitive workspace bytes and symlink components are
rejected because the reused CMake backend executes shell commands.

The supported path is SourceRecipe -> local source acquisition -> optional
SHA-256 transport digest/size verification -> private gzip tar extraction -> existing
CMake build/install into private staging -> Artifact Schema v1 scan -> local
ArtifactId derivation -> ObjectStore publication -> AmbientSourceBuild record.
The transport SHA-256 describes archive bytes; ArtifactId describes normalized
installed output. Repository ArtifactMetadata never substitutes for local
verification. SourceRecipe build_inputs are retained in the immutable plan but
produce no BuildInputId: ambient records always have an absent build_input_id.

C.4 supports exactly one source with a literal local `file:///absolute/path`
URI (no authority, percent encoding, query or fragment). Repository-relative
keys have no acquisition resolution contract here and are rejected, as are
remote transports and other digest algorithms. Empty sources are already invalid at the public plan
boundary; multi-source recipes are explicitly unsupported by execution.
Non-empty build_parameters are unsupported. IR references are rejected rather
than reinterpreted. ImportedBinary and BuiltFromAthenaIR realization remain
unimplemented, including artifact-only candidates. Autotools is deferred because
its current backend ignores the dedicated build directory. CMake is the only
supported backend in this executor.

Private extraction uses libarchive rather than the legacy shell extraction
helper. Its intentionally narrow gzip tar subset requires one top-level directory
and rejects traversal, duplicate paths, shell-sensitive path bytes, links,
special entries and extended permissions. No authoritative object is used as
writable staging. Builds are ambient, neither sandboxed nor claimed reproducible;
source-controlled build programs execute with the caller's privileges.

Execution errors distinguish invalid input, unsupported kind/recipe, acquisition,
source verification, extraction, build, artifact identity, publication and cleanup.
Wrapped stage failures retain the original exception_ptr, including ObjectStore's
integrity categories. Temporary resources are cleaned best-effort on failure;
cleanup diagnostics are attached without replacing the original failure. A cleanup
failure after otherwise successful publication has its own error category.

Realization creates no generations, changes no current-generation pointer, and
performs no activation or legacy store insertion. Publication is the transactional
boundary for each object, not the entire plan. Already-published immutable objects
may remain unreferenced after a later selection fails; content-addressed GC handles them.
Generation creation, activation, semantic install/remove and GC are integrated.
Remote acquisition, multi-source execution, build parameters, binary import,
Athena IR execution and BuildInputId derivation remain C.5 or later work.

M2.5-C requires a caller-owned `TransactionPins` handle in
`ExecutionOptions::transaction` before any publication. Ordinary records do not
own lifetime protection; callers retain the handle through durable generation
handoff or explicit abandonment. Failure/destruction never automatically retires
registered pins. Preparation/build remains outside StoreGc.
