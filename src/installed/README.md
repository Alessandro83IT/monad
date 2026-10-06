# Installed semantic state (M2.4)

Installed validation is distinct from repository resolution. It parses the
existing dependency grammar/AST and evaluates the actual resulting membership;
it never calls the resolver, reads a repository, or requires the original recipe.
`validate_environment` is pure. `capture_legacy_semantics` is an explicit read-only
compatibility adapter for old store metadata.

## Durable values

RealizationRecord v2 contains SemanticState v1 alongside its existing complete
CandidateIdentity, ArtifactId, origin and optional BuildInputId. SemanticState
reuses PackageMetadata for dependency expressions, capabilities, offered features,
conflicts, target restrictions and feature dependencies. It retains the effective
active roles, optional/propagated switches, configured features/profile, Host/Build/
Target values (including triples), version scheme and selected provider identities.
No source payload, transient search state or writable object locator is persisted.
The public model supports package-version capability providers; distinct capability
versions remain unsupported by the public repository bridge and are rejected there.

Provider bindings retain original resolution associations as audit provenance;
old versions/snapshots never substitute for current member facts. Validation
searches deterministic installed-provider assignments using the resolver's shared
capability/feature assignment key. All positive consumers of a semantic name must
use one provider satisfying their complete boolean requirements. An alternative
provider is accepted only as part of a coherent global assignment. NOT and conflicts
inspect all installed facts, including unselected providers. Search uses sorted
semantic keys and PackageIds and never consults a repository. The finite search can
be exponential in the number of independent semantic provider choices.

Generation v3 retains independent PolicyRequirement v1 values. Requirements outlive
selected packages, so removing all providers cannot erase a system policy. An
explicit later plan can replace the requirements of a named policy; ordinary
install/remove preserve other policies. No new CLI policy-management surface is
introduced. Policy selectors must be unscoped atomic package/capability/feature
selectors; disabled, role/context-scoped and boolean wrappers are rejected.

## Validation and transitions

Structural support checking visits every AST branch before boolean evaluation,
including disabled features, inactive scopes, NOT and short-circuited alternatives.
Malformed/unknown kinds and enums throw; unsupported never becomes boolean false.
Conditional guards use the same supported environment/configured-or-offered-feature
semantics as the resolver. Package/capability guards are unsupported. Nested scopes
retain their role and environment; role-activity traversal is shared with resolution.
Missing environmental facts needed for a predicate or provider comparison fail
rather than validating their negation. This is semantic validation, not cross-build
execution or a claim of isolated Host/Build/Target toolchains.

Every resulting member is checked, including preserved consumers. Conflicts,
negative expressions, active feature dependencies and persistent system requirements
are evaluated offline. New conflicting feature configurations in the same profile
are rejected. Omitted values inherit the explicit configuration for that profile;
different profiles remain independent. If no profile value exists, feature guards
retain the resolver's offered-feature fallback and feature dependencies remain
unconfigured. Transitions materialize inherited configuration so removing/replacing
its original carrier cannot erase enabled requirements. Explicit configuration
changes conflicting with the existing profile fail conservatively. Modern members
must share platform/architecture/ABI target facts for their single activation
namespace; Host/Build requirements still use the owner's distinct environments.
Triples remain provenance, as in the current resolver. Selected provider
artifacts/identities are unchanged by this layer.

`prepare_install` removes replaced memberships from a detached current value and
validates preserved plus planned semantic values without fabricating ArtifactIds.
The CLI performs this before creating runtime directories; the transaction repeats
preflight before realizing anything. Only actual executor records are persisted.
Removal validates the resulting generation before generation creation or activation.
Semantic failures therefore do not publish objects or write state/target files.
Transactions still require external serialization: M2.4 adds no locking, crash
journal, fsync or stronger exception-time filesystem rollback guarantee.

Generation switching validates every schema-3 target (and older targets carrying
M2.4 semantic metadata) before
activation, in addition to existing object integrity checks. ArtifactId addressing,
publication, activation projections and historical GC roots remain unchanged.

## Historical compatibility

Readers accept unversioned legacy generations, generation v2 and realization v1.
They never rewrite historical files or infer empty semantics for old realization
records. Pre-M2.4 generation input retains an explicit historical compatibility
mode and remains switchable under its historical integrity contract. Every new
write emits schema 3; absence of semantic fields cannot turn it into historical
state. Schema-3 unknown realizations remain readable but cannot be activated. An install/removal preserving a realization without semantics fails;
removing/replacing all unknown members is possible if the resulting state validates.

Legacy entries with an explicit dependency array can participate in a restricted
compatibility model: package references, package versions, boolean operators and
unversioned capabilities explicitly present in the old provides list. Configured
features, conditionals, environment/scoped requirements and externally supplied
rich fields are rejected because the old writer did not establish their meaning.
Absent dependency arrays are unknown, not empty. Old capability versions are
unknown: constrained capability queries involving a legacy provider fail, including
under NOT. Host/Build placement cannot be inferred for legacy providers.
Restricted legacy consumers validate their historical package requirements
against target memberships without inferring missing environment placement.

On successful transitions, captured restricted semantics are stored with surviving
legacy memberships in generation v3. Their subsequent validation is offline even
without the old metadata files. This explicitly validates the available legacy
contract; it does not claim previously unrecorded conflicts/configuration/provenance.
Duplicate historical legacy names remain decodable but cannot form a validated
resulting environment. No candidate or content identity is invented for them.

## Tests

`installed_semantics_test` exercises offline dependency/provider/feature/policy
transitions, scopes and guards, all roles, legacy capture, unknown-state rejection,
versioned codecs, history, CLI preflight and state/ObjectStore preservation. Checks
remain active in Debug and Release.

`installed_semantics_regression_test` exercises malformed nested feature metadata,
scoped policy rejection, global capability/feature assignments and boolean choices,
target coherence, profile inheritance and schema-3 activation rejection. Its six
cases can be run individually with H1/H2/H3/H4/H5/M1 arguments.

`installed_semantics_integration_test` runs actual CMake realization, CLI partial
removal, preserved-consumer upgrades/downgrades, offline rollback, list/info and GC.
It returns 77 only for the existing scanner requirement to inspect hidden trusted.*
xattrs. That integration requires initial-namespace CAP_SYS_ADMIN or a supported
filesystem explicitly reporting xattrs unsupported; no production inspection bypass
is added.
