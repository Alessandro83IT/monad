# Monad 0.1 Alpha M2 — stabilization and M1 qualification blockers

**Decision: NO-GO. Alpha M1 remains BLOCKED.** This report records source review,
a candidate-only source inventory, metadata hardening, and isolated unprivileged
Debug build/test evidence. It is not a qualification verdict and does not close
the approved Debian VM campaign.

## Scope and preserved starting state

- Branch `main`; `HEAD` at start and end: `abd6bac4b368427b76f6c98522afde928b418939`.
- The initial worktree was already dirty and contained untracked source, test,
  qualification, documentation, and populated build outputs. No reset, clean,
  commit, push, privileged invocation, installation, Alpha M1 collector, or
  Session 68 host driver was run. Existing paths and evidence were preserved.
- All build output for M2 was written to `/tmp/monad-m2-build`.
- Historical source/input inventories, historical bundles, installer pins,
  baseline reports, and qualification evidence were not edited. The candidate
  inventory and this report are separate new M2 artifacts.

## M1 report evidence reconstructed

The complete source document reviewed was
`docs/development/monad-0.1-alpha-m1-package-audit-20261027.md`. Its relevant
record is:

- M1 disposition is NO-GO/BLOCKED. Its local run reported Debug build PASS,
  116 PASS, 1 FAIL (`qualification_driver_test`), 7 SKIP, and 0 TIMEOUT. The
  seven skips were scanner-dependent integrations whose scanner could not prove
  absence of hidden `trusted.*` xattrs without initial-namespace
  `CAP_SYS_ADMIN`.
- That run excluded 18 `privileged`-labelled tests, six scanner-named tests,
  and one diagnostic case. It also acknowledged that its broad selection had
  mistakenly invoked the seven scanner-dependent integration tests before
  they were labelled. Each returned 77 before privileged work; they were not
  qualification evidence.
- The report describes pre-existing implementation of the package manager and
  M1 changes: a direct-argv process runner replacing shell interpolation for
  download, tar, and CMake/Autotools calls; package identity validation at
  metadata/path boundaries; and associated regressions. Those files were
  already modified in the M2 starting worktree and their earlier changes are
  not attributed to M2 here.
- The report distinguishes old B4 evidence (historical source bundle at a
  different baseline), synthetic Sessions 67–69, and actual VM/privileged
  qualification. Neither historical bundle nor synthetic evidence qualifies
  this current source closure.
- Open M1 blockers remain: source closure review, reviewed candidate build
  identity and security pins, production scanner physical proof, manifest
  schema strictness, build isolation/trust model, and the complete approved
  Debian VM privileged campaign with evidence review.

## Worktree-to-inventory reconciliation

`tests/qualification_driver_test.py` uses `tests/qualify_bounded_toml.py` and
`tests/bounded_toml_sources.txt`. The declared source roots are `src`, `include`,
`tests`, `cmake`, `third_party`, `packaging`, and `packages`, plus six fixed
root/document inputs. It omits generated `__pycache__` directories and rejects
symlinks. The official inventory lists 414 paths; discovery found all 414 and
12 additional files. There are no missing listed paths and no symlinks in the
reviewed roots.

The 12 differences were all already untracked at M2 start:

| Classification | Count | Paths |
|---|---:|---|
| Functional inputs | 2 | `src/process/run.cpp`, `src/process/run.hpp` |
| Tests | 10 | `tests/alpha_m1_collector_v2_test.py`, `tests/alpha_m1_collector_v2_trust_test.py`, `tests/alpha_m1_ctest_inventory_test.py`, `tests/alpha_m1_evidence_tree_digest_test.py`, `tests/alpha_m1_installer_transaction_test.py`, `tests/alpha_m1_receipt_path_test.py`, `tests/alpha_m1_verified_evidence_runner_test.py`, `tests/alpha_m1_verified_execution_bootstrap_test.py`, `tests/alpha_m1_vm_evidence_collector_test.py`, `tests/process_invocation_test.cpp` |

No documentation or generated artifact under the driver’s declared source roots
accounts for these 12 paths. Pre-existing documentation and qualification
reports under `docs/development`, `docs/qualification`, and `docs/security`,
qualification/admin tooling under `qualification`, `scripts`, and `tools`,
`.codex*` content, Python cache files, and the four populated build directories
are outside that declared source scope. They were preserved and were not
silently treated as inputs or deleted artifacts.

The separate candidate inventory is
[`monad-0.1-alpha-m2-candidate-inventory-20261027.json`](monad-0.1-alpha-m2-candidate-inventory-20261027.json).
It records 426 sorted discovered inputs with individual SHA-256 hashes,
classification, fixed scope, source `HEAD`, and the 12 pre-existing untracked
paths. Its generator is
[`monad-0.1-alpha-m2-candidate-inventory.py`](monad-0.1-alpha-m2-candidate-inventory.py).
Reproduce with:

```sh
python3 docs/development/monad-0.1-alpha-m2-candidate-inventory.py
sha256sum docs/development/monad-0.1-alpha-m2-candidate-inventory-20261027.json
```

At generation, the JSON SHA-256 was
`ceff8fbcf4f7e91018f6ebfaf05cf662859a8ee33bca10da1fbf11b9fdb07398`.
Regeneration intentionally reflects current source bytes, so after any source
change its manifest hash will change. It does not replace or amend the 414-path
official inventory. Promotion requires owner review of each of the 12 inputs,
source freeze, regeneration and independent hash review, and an explicitly
reviewed change to the official qualification input list. Any installer/binary
pin change requires the separate security-sensitive promotion process. Neither
promotion occurred in M2.

The worktree also contains pre-existing modified functional and test files
listed in the M1 report, including `CMakeLists.txt`, archive/download/build,
package identity, repository, and store code. Those are changes relative to
`HEAD`, not changes created by M2. M2 added a small overlay to pre-existing
`CMakeLists.txt`, `src/archive/archive.cpp`, `src/store/store.cpp`, and the
pre-existing untracked `tests/process_invocation_test.cpp`; it added
`src/store/manifest.cpp` and `tests/store_metadata_test.cpp` edits, plus the two
new documentation/inventory artifacts above. The exact composed bytes are
covered by the candidate hashes; the starting status above is the provenance
boundary.

## CAP_SYS_ADMIN test classification

The following seven CTest cases depended on the scanner’s initial-namespace
`CAP_SYS_ADMIN` requirement but lacked the `privileged` label. Each now has the
existing `privileged` label and retains its original `SKIP_RETURN_CODE 77`,
timeout, and execution logic:

- `installed_semantics_integration_test`
- `cli_install_integration_test`
- `install_transaction_integration_test`
- `generation_activation_test`
- `realization_executor_integration_test`
- `removal_realization_integration_test`
- `immutable_object_publication_test`

The nonprivileged run used `-LE 'privileged|diagnostic' -E 'scanner'`, so these
cases and all other privileged/scanner-named cases were excluded before
invocation. CTest reported 148 registered cases, 117 selected and 31 excluded.
All seven above were among the excluded 31. Their status is NOT RUN in this
session, not PASS or SKIP. Their actual scanner limitation remains visible in
their existing return-77 behavior and is not reclassified.

## M2 security review and changes

### Archive extraction and path collisions

The previous extraction directory was derived from the archive filename and
unconditionally removed before extraction. An unrelated or stale directory
with the same derived name could therefore be deleted, and concurrent
extractions of same-stem archives could interfere. Extraction now atomically
creates a unique per-process/per-call directory, never removes a pre-existing
name, and requests tar not to preserve owner or archive permissions. The
regression creates the former colliding directory and proves its sentinel
survives. Top-level entries are required to consist of one directory; files or
other top-level entries cause rejection.

Extraction still uses external `tar`. This change does not establish an
independent proof of every tar implementation’s symlink, hardlink, traversal,
collision, or resource-limit behavior. A future hardening pass should replace
or formally constrain extraction with explicit path/type validation and
symlink-safe filesystem operations, and add malicious archive fixtures.

### Staging symlinks and store integrity

The legacy store used `directory_entry::is_regular_file()`, which follows a
symlink; copying then imported the target bytes, including from outside staging.
The store now uses `symlink_status` to reject symlinks and special file types
before it creates the destination, checks each entry again while copying, and
rejects a symlinked `staging/usr` root. Regressions cover an outside-target
file symlink and a symlinked root; rejection occurs before a partial destination
is created. The current legacy manifest models regular files only, so rejecting
links is explicit fail-closed behavior rather than preserving symlink package
contents.

### Manifest encoding and path validation

Manifest paths were interpolated directly into quoted TOML, so legal filenames
containing quotes or newlines could corrupt the document. The writer now uses
the TOML serializer, and the round-trip regression covers a quote/newline path.
Both read and write validate paths as relative, normalized paths without `.` or
`..` components and disallow the reserved `.athena` metadata subtree. A
traversal regression is included. Duplicate manifest entries remain accepted
because an existing activation regression deliberately uses a duplicate to
exercise rollback after the first link is created; activation’s rollback
behavior remains tested rather than being bypassed by early duplicate rejection.

### Transactions and rollback

No administrative or real installation transaction was run. Existing
unprivileged transaction, publication, activation, crash, and rollback tests
were part of the selected CTest suite; all selected cases except the inventory
driver passed. The store’s legacy file-copy path is not converted into a fully
atomic temporary-directory publication by this session. A failure after some
regular files have copied can leave an incomplete destination for later
recovery/replacement. Immutable object publication uses its separate durable
publication protocol; that does not automatically establish equivalent
properties for the legacy store API. This remains a residual risk.

## Build and test evidence

Environment: GCC 14.2.0, CMake Debug, isolated build directory
`/tmp/monad-m2-build`. No live network, package installation, scanner campaign,
privileged test, Alpha M1 collector, or Session 68 host driver was run.

| Check | Result | Details |
|---|---|---|
| Full Debug configure | **PASS** | Fresh configure in `/tmp/monad-m2-build`; OpenSSL, LibArchive, Threads, Python found. |
| Full Debug build | **PASS** | Initial complete build succeeded; after final source edits, complete incremental build succeeded. |
| Full selected unprivileged CTest | **FAIL — 1** | 116 PASS, 1 FAIL, 0 SKIP, 0 TIMEOUT across 117 selected tests. Failure: `qualification_driver_test`. |
| Inventory driver | **FAIL — expected source-closure blocker** | All seven subtests errored in setup because the official 414-path list omits the 12 files listed above. No baseline edit was made to turn it green. |
| Focused follow-up after manifest/store changes | **PASS — 4/4** | `store_metadata_test`, `activation_test`, `generation_test`, `removal_e2e_test`. |
| Final focused archive/store rerun | **PASS — 2/2** | `process_invocation_test`, `store_metadata_test`; includes extraction collision, staging links, TOML escaping, traversal. |
| `git diff --check` | **PASS** | No whitespace errors. |
| Privileged/scanner/diagnostic exclusions | **NOT RUN — 31** | Excluded before invocation by labels/name. Includes all seven newly labelled tests. |
| Approved Debian VM campaign / M1 evidence review | **NOT RUN** | Required qualification gate remains open. |

The 31 excluded tests are not folded into PASS, FAIL, or SKIP. There were no
selected tests returning skip code 77 and no selected timeouts. The inventory
driver failure remains a failure even though the exact mismatch is now captured
in the separate candidate artifact.

## Residual risks and approval gates

1. **Source closure:** the official input inventory still mismatches the
   worktree. Owner review and explicit promotion of the candidate are required.
2. **Privileged physical proof:** the seven scanner-dependent integrations and
   all other required privileged cases still need the approved Debian VM
   campaign, preserved evidence, and independent evidence review.
3. **Candidate pins:** source changes invalidate prior candidate build identity.
   Rebuild and reconcile security-sensitive pins only through reviewed
   promotion; no pins were changed here.
4. **Archive parser boundary:** extraction remains external `tar`; full
   adversarial symlink/traversal/type/collision/resource-limit coverage remains
   open.
5. **Legacy store transactionality:** store copying is not atomically staged and
   published as a whole; incomplete entries after mid-copy failures remain a
   recovery concern.
6. **Build trust and schema:** package build scripts still run with the
   invoking user’s authority, and the M1 report’s legacy manifest schema
   strictness concern remains open.

## Decision

**NO-GO. Alpha M1 remains BLOCKED.** The unprivileged package-manager build and
selected regression suite provide local evidence only. The inventory failure,
31 NOT RUN cases, outstanding candidate/pin review, and absent approved Debian
VM evidence prevent qualification or release approval.
