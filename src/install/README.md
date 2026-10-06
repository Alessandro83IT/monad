# Install transaction (M2.2-C.5)

`transaction.hpp` provides the internal `install::install_plan` overload for
`core::InstallPlan`, an initialized `GenerationManager`, authoritative
`ObjectStore`, C.4 `ExecutionOptions`, and explicit target root. A caller can use
`PathLayout::generation_state` with the manager's `GenerationDirectoryTag` and
`PathLayout::target_root`; ObjectStore and workspace locators remain explicit.
The legacy installer/CLI is unchanged.

The transaction first calls C.4 `realize()` for the complete const plan. Until
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
the manager before nonempty execution. CLI, removal, and GC migration remain
outside C.5.

`install_transaction_test` checks capability-independent no-op/failure boundaries.
`install_transaction_integration_test` exercises actual C.4 publication and C.3
activation, membership, replacement/preservation, shared artifacts, later
realization failure, creation failure, and activation rollback. It returns 77
only for the existing hidden trusted.* scanner inspection limitation.
