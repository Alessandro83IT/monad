# Monad 0.1 Alpha M6 — reconciliation inventory, pins, and provenance

**Decision: NO-GO. Alpha M1 remains BLOCKED.** This report reconciles the
M1–M5 development records, the M2 candidate inventory, and the Alpha M1
qualification references visible in this checkout. It makes no qualification
baseline or pin changes and does not authorize staging or installation.

## Scope and method

- Repository `/home/alessandro/Projects/monad`, branch `main`, `HEAD`
  `abd6bac4b368427b76f6c98522afde928b418939`.
- The checkout was already dirty and contained user edits, untracked evidence,
  and populated build directories. They were preserved. This report is the only
  repository file created by M6.
- Read in full: M1 through M5 development reports; the M2 inventory generator;
  AAP-v1, recovery, administrative staging, Session 48 verified-launch, and
  Sessions 68–69 reports/candidates. Pin history was cross-checked against the
  installer, reconciliation records, and session reports.
- Candidate reproduction ran the M2 generator logic with its output redirected
  to `/tmp/monad-m6-candidate-inventory.json`. The committed-worktree M2 JSON
  was not overwritten. Its hash and the reproduced hash were compared, as were
  every path and per-input digest.
- No compilation or test case was run in M6. No privileged test, scanner,
  collector, Session 68 host driver, installation, credential read, network
  action, or administrative operation was performed.

## M1–M5 evidence reconciliation

| Session | Work and local evidence | Qualification consequence |
|---|---|---|
| M1 | Debug build PASS; selected suite 116 PASS, 1 FAIL (`qualification_driver_test`), 7 SKIP, 0 TIMEOUT. Six scanner-named, 18 privileged-labelled, and one diagnostic case excluded. The seven scanner-dependent cases returned 77 before privileged work after being selected inadvertently. | Source closure mismatch, pin/build identity, scanner physical proof, and Debian VM campaign remain open. |
| M2 | Debug build PASS; selected suite 116 PASS, 1 FAIL, 0 SKIP, 0 TIMEOUT; focused follow-ups PASS. Recorded 414 official inputs plus 12 unlisted inputs and created a separate 426-entry candidate JSON. | Candidate was explicitly not promoted. Official input list and pin references remain untouched. |
| M3 | Six focused tests PASS, including synthetic archive/security and store cases. | Source-level archive/store risks partly addressed; no broad run or privileged qualification. |
| M4 | Reported 108/108 selected nonprivileged PASS; additional selection/checkpoint cases were NOT RUN where outcomes were unreliable; separate targeted tests PASS. | Process-crash fixture coverage is not power-loss evidence. Source closure, pins, scanner, and VM campaign remain blocked. |
| M5 | Reported suite 108 PASS, 1 FAIL (`qualification_driver_test`), 0 SKIP, 0 TIMEOUT; `activation_selection_test` and four plan persistence boundaries PASS. An exploratory 60-second invocation timed out, then the complete CTest selection passed. | Local state-machine evidence improved; candidate provenance and all external/privileged qualification gates remain open. |

These are session-reported outcomes, not reruns in M6. In particular, no local
PASS count establishes binary provenance or qualifies the target filesystem.
Sessions 67–69 are synthetic development/orchestration evidence only. Session
68 explicitly says real Codex/auth execution was NOT RUN; Session 69 says live
GitHub acquisition and Codex launch were NOT RUN. Neither is Alpha M1 evidence.

## Historical and candidate source inventories

The official bounded-TOML input file (`tests/bounded_toml_sources.txt`) defines
the 414-path historical inventory scope: seven declared source roots plus six
fixed inputs, with `__pycache__` omitted. M2's report records that all 414
listed inputs were present, with no missing entries and no symlinks, and that
the qualification-driver discovery found 12 further files. The historical
inventory remains the qualification reference; it was not changed.

The 12 additional M2 inputs and their recorded functional roles are:

| M2-only input | Count | Role / provenance recorded by M2 |
|---|---:|---|
| `src/process/run.cpp`, `src/process/run.hpp` | 2 | Functional production input: shared direct-argv process invocation used to avoid shell parsing in downloader, archive, and build paths. They were untracked at the M2 starting point and therefore absent from the official file list, despite being required source. |
| `tests/alpha_m1_collector_v2_test.py`, `tests/alpha_m1_collector_v2_trust_test.py`, `tests/alpha_m1_ctest_inventory_test.py`, `tests/alpha_m1_evidence_tree_digest_test.py`, `tests/alpha_m1_installer_transaction_test.py`, `tests/alpha_m1_receipt_path_test.py`, `tests/alpha_m1_verified_evidence_runner_test.py`, `tests/alpha_m1_verified_execution_bootstrap_test.py`, `tests/alpha_m1_vm_evidence_collector_test.py` | 9 | Qualification/security/tooling regressions for collector v2, trust checks, inventory, evidence digest, transaction, receipt paths, verified runner/bootstrap, and VM collector. M2 records these as pre-existing untracked tests, not generated files. |
| `tests/process_invocation_test.cpp` | 1 | Functional regression test for direct argv execution and downloader/extractor/build invocation. Pre-existing untracked test at M2 start. |

Together, these are the 12 exact M2 additions (2 production sources + 10
tests). Their presence in the candidate is evidence of scope discovery, not an
independent attestation of authorship, review, or promotion. M2's provenance
statement is limited to their untracked status at that session's start.

### Reproduction and exact comparison

The M2 candidate artifact is
[`monad-0.1-alpha-m2-candidate-inventory-20261027.json`](monad-0.1-alpha-m2-candidate-inventory-20261027.json).
Its recorded and rechecked SHA-256 is
`ceff8fbcf4f7e91018f6ebfaf05cf662859a8ee33bca10da1fbf11b9fdb07398`.
It contains 426 sorted inputs and status
`CANDIDATE_ONLY_NOT_PROMOTED`.

Reproduction used the same generator and input roots, with only the output
destination redirected to `/tmp`; the output there contains 427 inputs and has
SHA-256
`a8638d42195f7fb044da7b938bc3454d2b612ddada6c4ab29be1ec8e36badb48`.
Comparison result: **426 old / 427 current; path sets differ by one addition,
with no removal; six shared path digests differ.**

| Current delta from M2 candidate | M2 candidate SHA-256 | Reproduced SHA-256 |
|---|---|---|
| `CMakeLists.txt` | `2a9f22bbc76ee52b94318cdf55a0aec699f7e0989e6cc808672bf971027fa2a8` | `a1e175e2221268a8c47f5e45c1e0a0750fbdaa83c074b4b332d1b090b4cf801e` |
| `src/archive/archive.cpp` | `8ac698aacc48ffe08b0857ec1f04f9f6d41beb369e893ea6bce1a92ab50b9725` | `ba0e5f8d2310f05f032aa9d716e05b414eea47e37c98384ffbb7925035ba5c1a` |
| `src/store/store.cpp` | `23d25e4af8fd3ea06644650cdfb67fa58e125e1fce84fe538b5d5d80e2909e38` | `2f77d0041d30c692c6bb6673db8b241e37bbd5be39a5fae44b21b5b3711b7f74` |
| `src/store/store.hpp` | `5fbceab20423c2155169d2fb8c5829da459f5661b380fb7be38dcd0d4e288b98` | `5271dbb375fd61be0620e08e5cfe05bde35c3dcc717543d79b55394d5c073343` |
| `tests/process_invocation_test.cpp` | `f3a0f051fb37682bee05983e06faa4b288f9824a41c409a41ddb1243b24ec790` | `dc163ed2d941d23473664e90e118cfe1ab7cf2b59821e745722d9c068f8fb65e` |
| `tests/store_metadata_test.cpp` | `f251c42584a5d397c36c668854df00706314ab8f117e76eb355297293809a3f5` | `233d328e74ed53a0a1208d976e02ed6a31de8745acfd77e4d02335975aec6290` |
| Added since M2 | — | `tests/archive_security_test.cpp` (untracked test input; covered by M3 report) |

The six content changes are consistent with the M3/M4/M5 source/test work
described in the reports; the exact bytes are captured above, but this does not
establish an approved source freeze. The one added path is the archive
security regression test. The current candidate inventory is reproducible as
a local snapshot, but **the M2 candidate itself does not reproduce from the
current worktree** and no candidate has been promoted.

## Alpha M1 pins and status

“Matches” below means exact local digest equality only. It does not mean the
bytes are approved, present on the VM, produced by a proven build, or qualified.

### Administrative artifact pins

The current installer embeds four pins. All four match the current checkout:

| Artifact | Embedded pin | Current file hash | M6 classification |
|---|---|---|---|
| `qualification/athena-alpha-m1-runner.py` | `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` | same | Byte-valid, but a security-sensitive administrative pin pending qualification use approval. |
| `qualification/gateway_support.py` | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` | same | Byte-valid, but pending qualification use approval. |
| `qualification/athena-alpha-m1-sudoers` | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` | same | Byte-valid draft identity; not proof of installed sudoers syntax/effective policy. |
| `qualification/athena-alpha-m1-systemd-canary.py` | `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` | same | Byte-valid, but runtime canary and administrative approval remain absent. |

These values agree with the Session 34B pin-closure candidate and Session 44
local reconciliation. Session 34B itself says no administrative promotion or
installation occurred and VM-side evidence is absent. Accordingly they are
**valid as current local references, not sufficient as operationally approved
pins**.

### Protected build artifact pins

The installer also pins seven build artifacts (six test executables and the
core shared library):

| Artifact | Current installer pin | M6 classification |
|---|---|---|
| `activation_execution_privileged_test` | `d87a84cbb1e18ba073beaadb1bb7737b61fa448081af37e4d4c5a6c29337d157` | Not verifiable against an authenticated candidate build in this M6 scope. |
| `activation_plan_retention_test` | `f76251d6e0af0bd892d1dcb17f41f80b13343c7c274e289014dfcb090370e474` | Not verifiable against an authenticated candidate build. |
| `activation_plan_persistence_test` | `f12620d89bb55e66f1e1960dd8852f2fdf27c9a148967d222d562fdddbd5aaa3` | Not verifiable against an authenticated candidate build. |
| `legacy_d3_test` | `61e23e8c9cd3a81c8766eb61e93e01cc7c9af2025a80f6056f28d3ac137ad6a9` | Not verifiable against an authenticated candidate build. |
| `initial_activation_test` | `934222dc181ae1a98a3d00bed5cba8e208cdad194d0c10fe552b1e5cf7e74243` | Not verifiable against an authenticated candidate build. |
| `libathena_core.so` | `e212420cb34aeca76cebe2ecf396832f25c3c4f119b846b42ef00d2994cc995a` | Not verifiable against an authenticated candidate build. |
| `CTestTestfile.cmake` | `99751015487882864d67b948dbbb0eee4ac7204aca1b7ee20435f8e69b656bd5` | Not verifiable against an authenticated candidate build. |

Session 34B/44 reported historical builds with 140 and 147 registered tests
and the exact same 18 privileged labels, but explicitly could not bind either
build to an authenticated source snapshot. The current worktree contains
several pre-existing build directories; their presence or CMake source path
does not demonstrate which source bytes produced their binaries. The
`/opt/athena-qualification` protected build referenced in historical reports
is not treated here as a candidate provenance artifact. No M6 binary-to-source
pin is verified.

### Historical pins, obsolete references, and unverified evidence

- The Session 30/32–34 pin history contains multiple proposed runner/support/
  canary values, including the initial installer values
  (`2290b78c…`, `86be3866…`, `0c256732…`) and later pin-only candidates. Those
  historical values are superseded by the present four installer literals for
  checkout comparison. They remain valid only as records of historical bytes;
  they must not be reused as current pins.
- Session 34B's installer digest
  `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7` and
  candidate manifest digest
  `00fd4036bf0f3873f5c2a0e932c912b4709460fa52c35a1c2564016a92811262` match
  current local files according to the report and M6 hash check. These identify
  candidate bytes; they are **not approval signatures or proof of deployed
  copies**.
- Session 40–44 launch pins for the verified evidence runner/bootstrap and the
  fixed VM collector are separate from the four installer pins and the seven
  protected build pins. They identify specific historical launch procedures;
  later runner bytes differ. Their locally recorded identity cannot be
  inherited as current execution authorization. Collector and runner execution
  remain NOT RUN in M6.
- Session 68 candidate hashes identify supervisor files, not the package
  manager or privileged qualification artifacts. Session 69 is orchestration
  code and synthetic test evidence, not a release pin set.
- A matching digest authenticates neither who approved the bytes nor the
  source/toolchain provenance. Historical manifests with build paths and hashes
  are insufficient where they lack an authenticated source snapshot and
  reproducible build record.

## Source, build, and artifact provenance

The evidence chain currently has these breaks:

1. **Input closure:** official inventory has 414 paths; the M2 candidate had
   426; current reproduced snapshot has 427. The 12 M2 omissions and later
   archive test require explicit reviewed treatment. M2/M5 report
   `qualification_driver_test` FAIL, so the checked-in official driver does
   not accept the current worktree as closed.
2. **Source identity:** `HEAD` is stable at the recorded commit, but the working
   tree has numerous modified tracked files and untracked functional/test
   inputs. A commit ID alone does not identify those bytes. The M2 JSON is a
   candidate snapshot and its source hashes have drifted as shown above.
3. **Build binding:** CMake cache source paths, test counts, and matching
   artifact hashes do not bind object files to the candidate source snapshot.
   Historical reconciliation explicitly says both protected and isolated build
   provenance are blocked.
4. **Reproducibility:** no reviewed clean source archive/tree digest, toolchain
   lock, complete configure/build log, dependency identity manifest, and
   independently reproduced artifact digest set is available here as one
   authenticated chain.
5. **Deployment:** no current VM-side transfer manifest, receipt, installed
   ownership/mode/ACL/hash acquisition, or approved Debian campaign evidence
   was reconciled in M6. Local candidate hashes do not prove deployed bytes.

Evidence still needed: an owner-reviewed frozen source closure; a separately
approved inventory promotion; a clean candidate build with toolchain and
dependency identities; full configure/build/test logs; hashes of the resulting
executables and libraries; independent reproduction or review of the artifact
manifest; and a protected transfer/deployment record binding those artifacts
to the approved VM snapshot. The specified Debian VM campaign and independent
evidence review remain additional required gates.

## Reconciliation proposal for M7

This proposal changes no reference files. A separate owner/security approval
record should authorize each promotion step before it is applied:

1. Freeze and review the exact source tree, including all modified tracked
   files and untracked functional inputs. Classify the 12 M2 inputs and the
   later `tests/archive_security_test.cpp`; decide the exact official inventory
   scope and generated-file exclusions. Preserve the old 414-path list and M2
   candidate unchanged as historical evidence.
2. Generate a new versioned candidate inventory into a new evidence path.
   Independently rerun enumeration and SHA-256 checks; require zero unexplained
   additions, removals, symlinks, or digest drift. Record source commit, dirty
   status, tree/input-manifest digest, tool version, and reviewer identities.
3. Reconcile every installer pin against authenticated source/build evidence.
   Do not derive or write a replacement from an existing workspace build.
   Review administrative pin changes as a distinct promotion with exact
   old/new values and installer hash.
4. Build from the frozen candidate in a new isolated directory. Record
   compiler, CMake, libraries, dependency versions, configure flags, complete
   logs, test inventory, and each artifact digest. Independently reproduce or
   review the artifact manifest. Preserve all existing build trees.
5. Obtain separate security/operations review of exact source, inventory,
   installer, runner/support/sudoers/canary, build artifacts, and transfer
   manifest. Any stale or unavailable item remains blocked. Approval of a
   candidate does not itself authorize VM installation.
6. Only after the independent administrative decision, execute the documented
   disposable-Debian-VM procedure, preserve D6 and all evidence, run the exact
   required privileged cases, and complete independent evidence review. No
   release decision is inferred before that review.

Risk of premature promotion: omitted or changed inputs can make a build
identity incomplete; an incorrect pin can accept unintended administrative or
binary bytes; and pathname/build-cache evidence can give false confidence in
binary provenance. Risks are controlled by preserving old references,
versioning the candidate, independent hash review, and separating candidate
approval from administrative authorization. Approvers and change references
must be recorded outside this proposal; no approval is asserted here.

## Residual blocks and decision

- Official source inventory mismatch and candidate drift: **BLOCKED**.
- 12 historical M2 additions: classified and compared, but not formally
  promoted; one later test addition also requires inventory review.
- Current four administrative pin strings: **byte match locally**, approval
  for qualification use remains **NOT ESTABLISHED**.
- Seven protected build pins: syntactically valid digests, but not verified
  against a proven candidate build; **BLOCKED**.
- Historical runner/collector and Session 68/69 pins: historical or
  scope-specific; not current Alpha M1 release authorization.
- Binary source provenance, clean reproducible build, transfer/deployment
  chain: **BLOCKED / NOT DEMONSTRATED**.
- Scanner physical proof, privileged Debian VM campaign and evidence review:
  **NOT RUN**.
- Legacy schema strictness, build-script trust/isolation, and real power-loss
  behavior remain residual technical risks documented by M1–M5.

**GO/NO-GO: NO-GO. Alpha M1 remains BLOCKED.** M6 establishes exact inventory
drift and pin findings, but lacks approved source closure, verified binary
provenance, and the mandatory VM/privileged evidence. No pin or official
baseline was altered.
