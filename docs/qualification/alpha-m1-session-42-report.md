# Alpha M1 — Session 42: verified runner execution

**Decision: GO TECNICO for the verified-execution byte invariant, conditional on the exact human-reviewed trust boundary documented here.** Core, Security, Verification, and Operations completed independent reviews after the inline-loader source/pin correction. The runner-source TOCTOU is removed: each execution stage hashes and compiles the same captured bytes, and the documented command is byte-identical to its tested loader constant. This is not operational authorization. VM identity, read scope, output locations, and explicit human authorization for any acquisition remain unfulfilled. Alpha M1 remains BLOCKED and installation is NOT AUTHORIZED.

**Duration:** approximately 35 minutes of active work and review at report preparation; no attempt was made to consume the planned maximum.

## Baseline and boundaries

- Repository `/home/alessandro/Projects/monad`, branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`.
- The working tree was already dirty with tracked edits, untracked qualification artifacts, reports, tests, and build directories. None were reset, cleaned, or deleted. The qualification artifacts supplied by prior sessions are untracked in this checkout; the Session 42 work updated only the runner document and runner code and added the bootstrap, architecture document, and tests/reports listed below.
- Collector SHA-256: `6527d34cbfcca75244a77a1457f13e3d8e1b79aee5393f348c73bc96d74a74d2` (unchanged).
- Historical 149-file digest: `f229f1b367f31053a283f564b3d2fa3edd713bfa1ede5134c899466f1bb8f67f`; the fixed read-only verifier returned 149 files and PASS. No historical file was modified.
- Six Session 34B files were rehashed and match the Session 34B closure report: runner `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def`, gateway support `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514`, canary `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2`, sudoers draft `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18`, installer `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7`, manifest `00fd4036bf0f3873f5c2a0e932c912b4709460fa52c35a1c2564016a92811262`.
- No runner invocation against the real checkout, no collector execution, CTest, privileged command, installation, canary, rollback, cleanup, system mutation, VM operation, commit, or push occurred. The tree verifier was read-only. Tests use only temporary synthetic payloads; the fixed-path CLI unit test mocks the snapshot function with a synthetic memfd and does not open the collector path.

## Verified execution design

The separately reviewed inline loader receives bootstrap bytes on stdin, bounds their size, checks their SHA-256, then compiles that same immutable byte object. The bootstrap opens each absolute path component with descriptor-relative `O_NOFOLLOW`, rejects symlinks, traversal, non-regular files, hard links, and out-of-range size, reads the runner once through the opened descriptor, checks stable metadata and the pinned digest, closes descriptors, rehashes the immutable `bytes`, and compiles those same bytes. No runner pathname is reopened for code loading. `runner.main()` also rejects calls without the bootstrap marker, including imported API calls.

The collector remains separately pinned and copied into a memfd. All four seals are applied and confirmed; the descriptor is rehashed before `/bin/bash` reads it through `/proc/self/fd/N`. The environment is allowlisted for Bash; Python launches under the documented `env -i`, `-I`, and `-S` invocation. These guarantees do not authenticate the inline loader unless its exact text/hash is independently approved, nor do they authenticate Python, the standard library, kernel, shell, VM, collector inputs, or output durability. The explicit command and trust boundary are in `qualification/alpha-m1-verified-execution.md`.

## Current identities

| Component | SHA-256 | Status |
|---|---|---|
| Inline loader source | `feef002511128263daf5fa05fcaa72686a688d58fb717b2f8d1a9d026b205021` | Test constant and extracted command bytes match; Verification independently confirmed; human approval remains separate |
| Bootstrap | `3a5aa58d0a87b1363e61180773b4fd0318b930d41b81186a39ff0fd40a71f9a6` | Current local bytes |
| Runner | `f67a5dde11add6ced5802fa8091680f75f480a43fcc386f2049844f313b2f469` | Current local bytes; new identity is not automatically approved |
| Collector | `6527d34cbfcca75244a77a1457f13e3d8e1b79aee5393f348c73bc96d74a74d2` | Approved fixed pin unchanged |
| Historical tree verifier | `b003d2b02dcbb02d304371524ef395838599fd918e1505bd100fc2a1340435e3` | Unchanged |

Prior Session 39–41 launch procedures and approvals retain their original historical meaning and old runner digest. Session 42's architecture document supersedes their direct runner invocation syntax for any future proposal. No old approval was silently revised or inherited.

## Tests

Command: `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v tests.alpha_m1_verified_execution_bootstrap_test tests.alpha_m1_verified_evidence_runner_test tests.alpha_m1_evidence_tree_digest_test`.

**42 PASS, 0 FAIL, 0 SKIP, 0 TIMEOUT.** Coverage includes valid and invalid hashes; absent, symlink, hardlink, special and oversized files; traversal and relative paths; replacement before open and after verification; concurrent mutation; descriptor closure; EINTR; syntax failure before payload; bootstrap digest failure; isolated import behavior; direct and imported runner gate rejection; synthetic signal delivery; memfd digest and seals; synthetic child failure/status/output; and historical digest framing and mutation detection. This suite does not establish behavior on the qualification VM or authenticate the Python/kernel/loader trust base.

## Independent reviews

- **Core:** After finding stale direct-launch documentation and missing loader pins, independently confirmed the corrected command and constant are byte-identical, all file pins match, and the execution argument/stdin flow is verification-only. **PASS; no blocking findings.**
- **Security:** After identifying the initial loader text/pin mismatch and recommending a `main()` marker gate, independently confirmed the corrected 626-byte command/constant match, pins, and gate behavior. **GO TECNICO for the byte invariant**, conditional on the reviewed loader/runtime/pins trust boundary. The hash chain is not a signature and requires separate human approval.
- **Verification:** Confirmed the loader command and test constant both hash to `feef002511128263daf5fa05fcaa72686a688d58fb717b2f8d1a9d026b205021`, reran the final three-module suite, and reported **42 PASS, 0 FAIL, 0 SKIP**. The real collector path is passed only to a mocked function and is not opened; the historical corpus was not re-read by this final test command.
- **Operations:** Confirmed the corrected command text/hash and verification-only argument flow; reviewed Debian 13 compatibility, pipeline behavior, signals, output handling, and the separate authorization boundary. **PASS for documented verification form; actual acquisition remains NO-GO.**

No review is represented as approval or as authorization to execute.

## Gate disposition

| Gate | Result | Basis |
|---|---|---|
| Runner bytes bound to executed Python code | PASS locally, review pending | Captured immutable bytes are hashed and passed directly to `compile`; race regression tests pass |
| Bootstrap trust root and current pins | PARTIAL | Exact loader text/hash and full bootstrap/runner hashes documented; external human approval and transfer verification absent |
| Collector pin and sealed payload | PASS locally | Fixed SHA and memfd seal/re-hash tests; no collector execution |
| Synthetic regression coverage | PASS | 42/42 targeted tests |
| Historical evidence integrity | PASS locally | 149 files and fixed aggregate digest reproduced read-only |
| Independent Security/Core/Operations final review | BLOCKED | Requested final follow-ups not yet returned at report preparation |
| VM identity/snapshot and VM-side verification | BLOCKED | Not accessible from this environment; no VM commands run |
| Capture path and operator authorization | BLOCKED | No exact approved VM-side output paths or human authorization |
| Alpha M1 qualification | BLOCKED | VM campaign and evidence review remain outstanding; 34 BLOCKED and 7 NOT RUN unchanged |

## Files created or modified

- Added `qualification/alpha-m1-verified-execution-bootstrap.py`.
- Modified `qualification/alpha-m1-verified-evidence-runner.py` to enforce bootstrap provenance in the imported `main()` API.
- Added `qualification/alpha-m1-verified-execution.md` and updated `qualification/alpha-m1-verified-evidence-runner.md` with current launch/trust documentation. Earlier session reports/procedures and the six Session 34B artifacts were not modified.
- Added `tests/alpha_m1_verified_execution_bootstrap_test.py`; updated `tests/alpha_m1_verified_evidence_runner_test.py` with an imported-entry gate regression.
- Added Session 42 core/main, security, verification, and operations reports (the latter three are to be finalized after their corresponding reviews return).

## Remaining gates

1. Independently approve and transfer the exact inline loader, bootstrap, runner, and collector identities through the human authorization channel.
2. Complete host/guest/snapshot identity and output-path preflight using the Session 41 read-only manual procedures; reconcile evidence on the actual VM.
3. Obtain a separate, specific human decision before any diagnostic collection. That decision cannot authorize installation, canary, privileged tests, rollback, or cleanup.

The runner has **GO TECNICO** for the scoped byte-integrity invariant. Operational acquisition remains **NO-GO** until the three gates above are satisfied and a human separately approves the exact action. Alpha M1 remains **BLOCKED**; this report does not authorize execution, installation, canary, privileged tests, rollback, or cleanup.
