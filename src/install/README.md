# Install transaction (M2.4)

`transaction.hpp` provides the internal `install::install_plan` overload for
`core::InstallPlan`, an initialized `GenerationManager`, authoritative
`ObjectStore`, C.4 `ExecutionOptions`, and explicit target root. A caller can use
`PathLayout::generation_state` with the manager's `GenerationDirectoryTag` and
`PathLayout::target_root`; ObjectStore and workspace locators remain explicit.
`athena install <package>` now loads the existing local package catalog through
the public LocalRepository adapter, calls resolve_request() and finalizes a
core::InstallPlan before entering this transaction. Store, build workspace,
generation state and target root come from the CLI's configured PathLayout.
Runtime directories and GenerationManager initialization happen only after
successful non-empty planning and complete-environment semantic preflight. The legacy installer remains compatibility code
(including install-test), but is no longer the CLI install path.

C.4 limitations still apply: one local file:///absolute/path gzip tar source,
CMake only, no build parameters, remote acquisition, binary import or Athena IR.
Unsupported recipes fail with diagnostics and never fall back to legacy install.
The reused local repository bridge preserves manifest conflicts and feature
dependencies. Distinct capability-specific versions are rejected because the
current public metadata model uses the package version for capability providers.

The transaction first reads the current generation and validates the complete
resulting installed environment with `installed::prepare_install`. Preserved
consumers, conflicts, features and policy requirements are included; replacement
candidates come from the const plan, not repository reselection. A semantic failure
has no publication, state or target writes. It then calls C.4 `realize()`. Until
all selections succeed it writes no generation metadata and performs no
activation. Published immutable objects can survive a later realization failure
as unreferenced objects. Empty plans are validated no-ops, even for a manager
that has not been initialized; they create no directories or generation.

After realization, membership is merged with the current generation by logical
package name. Selected names replace either legacy or realization members;
unselected members survive. RealizationRecords are used directly, without
Package conversion or copied object paths. `GenerationManager::create()` retains
C.2 validation and canonical realization order (legacy relative order survives).
C.3 `switch_to(id, target_root, objects)` owns verified activation, filesystem
rollback and current-pointer ordering. On activation failure the new inactive
generation is discarded best-effort, preserving the original exception. No
ObjectStore object is deleted during rollback.

Generation creation errors propagate before activation. If `create()` fails
without returning a generation, its API provides no safely owned generation ID
for cleanup; partial persistence cleanup remains that primitive's responsibility.
No new crash-durability, concurrency, or current-pointer persistence guarantees
are introduced. Callers must serialize environment transactions and initialize
the manager before nonempty execution. Removal now validates partial resulting membership using the same offline semantic
model. GC already derives content-addressed roots from every historical generation.
See [installed semantics](../installed/README.md) for codecs and compatibility.

`install_transaction_test` checks capability-independent no-op/failure boundaries.
`install_transaction_integration_test` exercises actual C.4 publication and C.3
activation, membership, replacement/preservation, shared artifacts, later
realization failure, creation failure, and activation rollback. It returns 77
only for the existing hidden trusted.* scanner inspection limitation.
