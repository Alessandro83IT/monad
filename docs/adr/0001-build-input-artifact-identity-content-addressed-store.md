# ADR-001 — Build Input Identity, Artifact Identity and Content-Addressed Store

**Status:** Accepted

## Context

M2.1 provides a pure public planning pipeline from InstallRequest through
resolution to an immutable core::InstallPlan. Planning preserves selected
repository candidates, dependency edges, environments, features, and reasons;
it does not fabricate build or artifact identities.

The existing executor has reusable acquisition, verification, extraction,
CMake/Autotools build, staging, store, generation, activation, removal, and GC
components. Its store uses `<store>/<name>-<version>`, which cannot distinguish
different realizations of the same package/version. Its manifests record only
regular-file paths. CMake and Autotools currently run in an ambient environment
whose effective build inputs Athena does not completely control or fingerprint.

M2.2 needs truthful realization identity and immutable publication before a
general executor can consume the richer public plan. A wholesale execution
rewrite is not required. This decision establishes architecture, not an
implementation or a claim that existing code already satisfies it.

## Decision

### Distinct identity layers

The following identities MUST NOT be treated as interchangeable:

| Layer | Meaning |
| --- | --- |
| Package identity | PackageId + PackageVersion: a logical package/version. |
| Candidate identity | CandidateIdentity: selected package/version, repository identity, snapshot identity, and provenance identifying a repository record. |
| Build input identity | BuildInputId: a complete controlled effective build description under a versioned canonical schema. |
| Artifact identity | ArtifactId: a locally verified normalized immutable realized output under a versioned artifact schema. |
| Store object identity/reference | ArtifactId is authoritative identity; a store reference locates the immutable object. |
| Generation membership | Association between selected package semantics, realized outputs, immutable references, and realization metadata. |

Mappings are not necessarily one-to-one:

- Different candidates may have identical BuildInputIds.
- One BuildInputId may produce different ArtifactIds when execution is
  nondeterministic.
- Different BuildInputIds may produce identical ArtifactIds.
- Imported artifacts may have no BuildInputId.
- Multiple candidates may reference the same ArtifactId and store object.

### BuildInputId

BuildInputId identifies a versioned canonical description of the **complete
controlled effective build inputs** required by the supported build schema.
It MUST NOT be PackageId, CandidateIdentity, RepositorySnapshotRef, ArtifactId,
or a hash of the complete InstallPlan.

Athena MUST NOT generate a complete BuildInputId unless it can truthfully
describe those inputs. Unknown BuildInputId MUST remain absent; Athena MUST NOT
fabricate one from incomplete recipe, package, repository, or environment data.

Controlled inputs may include recipe/build semantics, verified source contents,
patches and application order, backend and backend semantics, effective options,
build-visible feature configuration, exact dependency artifacts and placement,
toolchain/build-tool closure, host/build/target execution descriptions,
controlled environment, and Athena build semantics/schema version. A locator
alone does not establish the identity of recipe, IR, patch, or tool content.
The later build schema MUST specify its supported effective inputs; this ADR
does not freeze every future serialization field.

Acquisition URLs, repository labels, display metadata, and source sizes are
normally metadata rather than build identity. They contribute when their
values are actually exposed to or change execution semantics. Verified content
identity SHOULD replace acquisition location in the effective description.

### Build-input canonicalization and hashing

The initial identity scheme MUST use SHA-256, explicit domain separation, and
an explicit canonical schema version. Conceptually:

```text
BuildInputId = SHA-256(
    build-input-domain
    + schema-version
    + canonical-effective-build-description
)
```

The concatenation denotes an unambiguous canonical encoding, not unspecified
string concatenation. The encoding MUST:

- Be deterministic across runs, repository ordering, and unordered-container
  iteration.
- Define field boundaries, types, optional values, and deterministic map,
  list, and set treatment.
- Preserve meaningful operation/list order and explicitly canonicalize
  semantic sets.
- Exclude timestamps, PIDs, pointers/address values, and locale-dependent
  formatting.
- Avoid accidental identity from temporary or absolute filesystem paths.

Build-visible path behavior MUST be controlled or described explicitly; merely
omitting observable paths from the hash does not establish complete inputs.
SHA-256 identifiers MUST encode the algorithm using the initial `sha256:`
convention with a full lowercase hexadecimal digest. The domain and schema
version MUST participate in the hashed material. Public BuildInputId remains
an opaque typed value; identifier text MUST NOT conflate identity layers.

Hashing/validation failures MUST abort identity generation. Conflicting material
under an existing claimed identity MUST cause failure rather than replacement.

### ArtifactId and normalized output

ArtifactId identifies a versioned normalized immutable realized output,
independently of package, repository, profile, or producer origin. It MUST NOT
simply equal or be copied from BuildInputId. The initial scheme is:

```text
normalized realized output
    -> canonical artifact manifest/tree representation
    -> SHA-256(artifact-domain + schema-version + canonical-representation)
    -> ArtifactId
```

Artifact hashing MUST have explicit domain/version separation distinct from
build-input hashing and use the full `sha256:` digest convention. Identity MUST
be based on the locally verified normalized payload that will be published.

The representation MUST account for supported relative paths, file types,
regular-file contents, normalized modes/executable state, directory structure,
empty directories, and literal symlink targets. Hashing MUST NOT follow a
symlink as though it were the linked file's contents. Paths MUST be validated
against traversal outside the artifact.

Host-specific timestamps and ownership MUST NOT accidentally change ArtifactId.
Normalization MUST explicitly define any supported ownership/mode semantics.
Unsupported filesystem semantics that affect meaning, including unsupported
xattrs, ACLs, special permissions, device types, or hardlink behavior, MUST be
rejected rather than silently discarded. Supported normalization may transform
a representation only according to the declared schema.

Exact canonical encoding, supported metadata, output-layout rules, and the
relationships between manifest digest, output-tree digest, and ArtifactId MUST
be specified in a versioned artifact specification before implementation uses
them. The artifact manifest MUST describe the supported payload faithfully;
the current path-only manifest is insufficient. Administrative identity records
MUST NOT recursively hash their own ID. Package-specific provenance MUST NOT
be inserted into the shared payload in a way that changes content identity.

### Store addressing and immutable publication

The authoritative new namespace is conceptually:

```text
<store>/objects/v1/sha256/<full-digest>
```

ArtifactId, not package name/version, CandidateIdentity, or BuildInputId, MUST
address authoritative immutable objects. The full digest MUST provide identity;
human-readable aliases MAY exist but MUST NOT define it. Repository identity
MUST NOT be part of the authoritative object path. Identical verified artifacts
from different candidates or repositories MAY share one object.

Once successfully published under an ArtifactId, an object MUST NOT be modified
or replaced in place. Implementation MUST provide:

1. A private realization area.
2. Validation and explicit normalization of the supported payload.
3. Canonical manifest construction and local identity calculation.
4. Immutable finalization of payload and required object metadata.
5. Atomic, no-replace publication on the store filesystem.
6. Collision/duplicate verification and concurrency protection.
7. Safe cleanup of abandoned private work without touching published objects.

An existing object with matching verified content identity MAY be reused. An
object whose contents do not match its claimed identity is corruption and MUST
cause failure, not overwrite. Publication MUST handle concurrent producers
without replacing an existing object. Physical read-only permissions and
ownership are hardening, not content identity or protection against all
privileged mutation.

Unpublished work MUST NOT be treated as a store object. Crash durability and
cleanup guarantees MUST be specified by the publication implementation; atomic
namespace publication alone MUST NOT be advertised as full crash consistency.
An object published before a later generation failure may remain unreferenced
and be reclaimed through safe reachability-based GC.

### Provenance and realization associations

Content identity and provenance/audit identity MUST remain separate.
RepositoryId, RepositorySnapshotRef, and RepositoryProvenance MUST remain
available in realization/generation metadata. They normally do not participate
directly in ArtifactId.

Repository-derived data participates in BuildInputId only when it is an actual
effective build input; verified content identity SHOULD be used instead of
repository labels. Shared artifacts MAY have multiple immutable provenance
associations. Package-specific semantics and provenance MUST be recorded
separately from a shared object so one association cannot overwrite another.

Build-result associations MUST NOT assume one BuildInputId has exactly one
ArtifactId. Differing observed outputs MUST remain detectable rather than
silently replacing earlier observations.

### Ambient source builds

Existing native CMake/Autotools builds MAY initially produce a locally verified
normalized ArtifactId while BuildInputId remains absent. Absence is valid.
These builds MUST NOT receive a fabricated complete BuildInputId, claim complete
reproducibility, or use BuildInputId-based build-cache equivalence.

Realization/origin metadata MUST truthfully represent this non-IR source-build
case. It MUST NOT label an artifact BuiltFromAthenaIR unless the build actually
meets that public contract. A new metadata representation is an implementation
prerequisite, not permission to misuse an existing origin enum.

### Imported and repository-provided artifacts

Imported artifacts MAY have no BuildInputId. They MUST be acquired and verified,
normalized under a supported artifact schema, and locally assigned or verified
against ArtifactId before authoritative publication. Transport archive/source
digests and normalized ArtifactId are distinct identities.

Repository-provided ArtifactIds are claims until the normalized acquired payload
is locally verified against the supported scheme. Expected manifest/output
digests MUST be verified according to that scheme. Mismatches MUST fail without
publication. Unsupported external identity schemes MUST NOT be silently treated
as verified local identities.

A future producer-supplied BuildInputId is an attested claim unless Athena can
independently establish the corresponding canonical build description. No
producer identity may be synthesized for an imported artifact. Transport and
remote cache protocols are outside this decision.

### Dependencies, features, and profiles

BuildInputId MUST describe dependency artifacts actually exposed to or consumed
by a build, not every selection in ResolutionResult. Contribution follows
concrete provider artifacts, selected outputs/placement, role, context, feature
activation, and effective build environment. Role and context remain independent.

Capability labels alone are insufficient when execution consumes a provider
artifact. Runtime, test, development, optional, and propagated dependencies
contribute when their artifacts or operations are effective build inputs; there
is no universal dependency list or global execution order implied by this ADR.

Environment-satisfied dependencies lacking a controlled/fingerprinted input
representation may prevent generation of a complete BuildInputId. Athena MUST
NOT invent a provider identity to fill such a gap.

Feature state contributes when it changes inputs, patches, parameters,
environment, or operations. Profile names normally remain audit/configuration
labels. Differently named profiles with the same canonical effective build
description SHOULD converge to the same BuildInputId. Unsupported feature or
dependency execution semantics MUST NOT be silently ignored.

### Generations and reachability

Generation membership MUST evolve from `package + store_path` toward explicit
association of CandidateIdentity, ArtifactId/output reference, immutable store
reference, and installed semantic/realization metadata. Generation identity is
NOT ArtifactId. Generations MAY reference shared artifacts.

Generation/GC metadata MUST retain enough information for activation, rollback,
provenance inspection, dependency-aware removal, and safe reachability. It must
preserve relevant installed dependency semantics and effective configuration
without requiring repository reselection. Runtime references between artifacts
MUST remain reachable where needed; build-only provenance does not inherently
make every build input a runtime GC root.

This ADR does not implement generation migration or make current activation
atomic. Reliable activation/recovery remains a separate implementation obligation.

### Legacy store coexistence

Existing `<store>/<name>-<version>` entries remain explicit legacy objects and
MUST remain readable during migration. Existing generations referencing them
MUST remain valid. Name/version MUST NOT be converted into a synthetic ArtifactId.

Strong current-content identity may be established only by actual supported
normalization and hash verification. Even then, hashing current contents does
not prove original provenance or BuildInputId. Legacy and new namespaces MAY
coexist; GC MUST eventually understand both and preserve referenced objects.
Migration MUST NOT silently delete or rewrite historical legacy entries.

### Security terminology and guarantees

Integrity, authenticity, provenance, reproducibility, and semantic equivalence
are distinct:

- **Integrity**: bytes/normalized content match an expected identity.
- **Authenticity**: a claim originates from an authority accepted by trust policy.
- **Provenance**: recorded origin and production/selection associations.
- **Reproducibility**: repeated execution of the same controlled build description
  yields the same normalized output.
- **Semantic equivalence**: outputs have equivalent intended behavior; hashes do
  not generally prove behavioral equivalence.

| Mechanism | Establishes | Does not establish by itself |
| --- | --- | --- |
| BuildInputId | Equality of canonical declared controlled build inputs, subject to hash assumptions. | Completeness of capture, authenticity, or reproducibility. |
| ArtifactId | Identity/integrity of normalized output under its schema. | Trusted origin, safe code, or reproducibility. |
| Source digest | Acquired bytes match an expected digest. | Authenticity of the expected digest. |
| Manifest digest | Canonical manifest matches its expected digest. | Payload integrity unless payload is also checked. |
| Output-tree digest | Normalized payload matches its tree representation. | Trusted origin or correctness. |
| Repository provenance | Recorded origin. | Authentication of that origin. |
| Signature/attestation | An authority authenticates or endorses a claim under configured trust policy. | Independent reproducibility. |

SHA-256 provides practical collision resistance under current cryptographic
assumptions, not a guarantee that collisions are mathematically impossible.

### Minimum supported M2.2 execution path

The first executor MAY restrict support to digest-verified source input,
currently supported native CMake/Autotools operations, explicit supported
preparation semantics, supported normalized output, local ArtifactId calculation,
immutable publication, realization/provenance association, and generation
references. Unsupported configurations MUST fail explicitly.

The valid initial ambient source-build result is:

```text
ArtifactId = known and locally verified
BuildInputId = absent
```

This is preferable to a false BuildInputId. Public planning remains pure;
acquisition, building, identity realization, publication, and activation occur
after the immutable plan boundary. Execution MUST preserve selected identities
and MUST NOT re-resolve dependencies to conceal unsupported inputs.

## Relation to Nix

Athena is inspired by immutable/declarative ideas also found in Nix.
BuildInputId has a conceptual build-description identity role; ArtifactId is
content-oriented. Athena does not adopt Nix store semantics mechanically and
retains its own PackageId, CandidateIdentity, BuildInputId, ArtifactId,
ResolutionResult, and generation contracts. Nix behavior is not normative here.

## Deferred work

- Fully controlled build sandbox.
- Complete BuildInputId generation for ambient builds.
- BuildInputId-based build cache.
- Reproducibility verification.
- Multiple outputs.
- Full cross compilation.
- Unsupported patch/IR execution.
- Producer attestations.
- Advanced filesystem metadata.
- Remote artifact/binary cache protocol.

These deferrals MUST NOT weaken local ArtifactId verification, truthful metadata,
or immutable store publication.

## Consequences

Positive consequences include repository-independent content identity,
deduplication of identical outputs, truthful separation of build/artifact
identity, foundations for later caching and reproducibility, stronger corruption
detection, and explicit generation/GC references. Existing execution components
remain reusable behind these boundaries.

Costs include versioned canonicalization specifications, richer manifests,
dual legacy/new readers, realization and generation metadata evolution,
additional validation and concurrency handling, and eventual controlled build
environments. Artifact equality does not imply equal package semantics, so
package-specific associations must survive deduplication.

## Rejected alternatives

- **Store identity = package name/version:** collapses distinct realizations.
- **Store identity = CandidateIdentity:** prevents repository-independent sharing
  and does not identify realized output.
- **ArtifactId = BuildInputId:** confuses declared inputs with observed output and
  cannot represent nondeterministic results or unknown imported build inputs.
- **Hash the complete InstallPlan:** includes unrelated selections and intent
  rather than one effective build.
- **Hash complete repository snapshots:** changes identity for unrelated catalog
  changes and provenance labels.
- **Trust repository ArtifactId without local verification:** fails to establish
  payload integrity under Athena's supported scheme.
- **Fabricate BuildInputId for ambient builds:** falsely claims complete input
  identity and enables unsound cache equivalence.
- **Silently discard unsupported artifact semantics:** changes meaning without
  representing the change in an explicit schema.
- **Pretend legacy entries already possess strong ArtifactIds:** invents identity
  without normalization/hash verification and risks historical rollback.

## Current public-contract implementation prerequisites

At checkpoint `b8ed162`, these prerequisites remain for M2.2-B:

- ArtifactOrigin contains only BuiltFromAthenaIR and ImportedBinary. There is no
  truthful origin representation for current non-IR ambient source builds.
- ArtifactMetadata has an optional BuildInputId, but validation requires it for
  BuiltFromAthenaIR and prohibits it for ImportedBinary. Ambient-source absence
  requires a compatible versioned realization/origin representation. Future
  imported producer claims need separate/versioned attestation support.
- ImportedBinaryMetadata has optional archive digest and no producer BuildInputId
  or normalization identity scheme. Execution must require supported verification
  information; planning validity alone does not establish executable integrity.
- ArtifactMetadata provides opaque ArtifactId, output/manifest digests, and
  provenance, but no implemented canonical artifact scheme or retrieval locator.
  Execution specifications must define and verify supported local identities
  without reinterpreting arbitrary existing opaque values.
- SourceRecipe build_inputs is not a complete current-build description or a
  concrete mapping to consumed dependency artifacts. Its optional source digests
  do not authorize unverified execution.
- GenerationEntry contains only package name and store path. Store metadata is
  package-specific, manifests contain paths only, and store lookup uses
  name/version. Versioned identity references, complete manifests, separate
  associations, and dual legacy/new readers are required.

This document changes none of those types or implementations. Their evolution
must be implemented and tested in subsequent explicitly scoped phases.
