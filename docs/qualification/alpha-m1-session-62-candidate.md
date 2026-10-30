# Alpha M1 Session 62 — verified byte handoff candidate

**Verdict: `SESSION 62 PASS WITH CONDITIONS`. Alpha M1 remains BLOCKED.**
This is an unpromoted, synthetic-only candidate. No collector, historical
runner, diagnostic backend, systemd service, journal, real process/cgroup, or
D6 payload was accessed.

## 1. Architecture and boundary

The CMake production target remains the Session 61 verify-only executable.
`--test-handoff` exists only when the source is compiled with
`SESSION62_TESTING`; that macro also requires a build-time fixed interpreter
path. The synthetic harness creates its own candidate copies, AAP-v1
authorizations, target identity, and nonce ledger in temporary directories.
It executes one fixed, harmless entry point whose digest is pinned in the
test-only native source. It does not execute the collector or adapters.

Native manifest verification now retains the exact component byte strings
that were read through the existing descriptor-relative, bounded, no-follow
verification path. The test handoff never reopens application source
pathnames. After synthetic authorization and target checks, it durably
reserves the nonce, copies the retained bytes to one memfd per allowlisted
component, verifies writes and applies `F_SEAL_SEAL`, `F_SEAL_SHRINK`,
`F_SEAL_GROW`, and `F_SEAL_WRITE`. It checks the seals, duplicates only the
allowlisted descriptors for child inheritance, and invokes the pinned test
interpreter using `fexecve` on an already opened descriptor.

The interpreter executes a fixed inline loader with `-I -S -B` and a minimal
environment. The loader verifies descriptor count/order, descriptor
uniqueness, regular-file type, size, required seals, stable metadata, bounded
reads and SHA-256. It loads application source using `compile`/`exec` from
those in-memory bytes through a fixed import hook, with empty `sys.path` and
bytecode disabled. Only the fixed package initializer, `contracts.py`,
`results.py`, and harmless `synthetic_entry.py` are expected to load. The
entry point and the three imported source modules are digest-pinned in native
test code as well as being bound by the candidate manifest. The child reports
digests and module decisions; native code checks the canonical result and
fixed expected values before attempting synthetic nonce consumption.

The child receives `/dev/null` for stdin/stderr; stdout is bounded to 64 KiB.
A start pipe distinguishes a Python loader that reached its first instruction
from a pre-exec failure. The parent polls through stdout EOF until the child is
reaped or the deadline expires. It kills the isolated process group on
timeout/output overflow, allows a bounded cleanup interval, then returns with
`runtime_cleanup_incomplete=true` if channels or child state remain. An
uninterruptible kernel task may still outlive the parent; process groups are
not a cgroup or namespace sandbox.

## 2. Invariant matrix

| Invariant | Session 62 evidence | Limit |
|---|---|---|
| No application byte executes before verification | Synthetic entry and import-source digest pins are checked after native candidate verification and before reservation/handoff; altered entry/import tests reject before Python starts | Demonstrated only for the test binary and fixed fixture, not for an authenticated production bootstrap |
| Executed application bytes equal measured bytes | Native retains measured byte strings; memfd copies are sealed; child rechecks descriptor seals and digest; parent checks child digest report; pathname replacement tests still execute retained bytes | Trusts native binary, kernel memfd/seal implementation, interpreter, stdlib, and child result channel |
| No application component outside the allowlist is imported | Empty `sys.path`, fixed `MetaPathFinder`, exact descriptor list, expected loaded-module list, and blocked unlisted/extension-like imports | Python runtime and standard-library import machinery are trusted, not attested; not a general Python sandbox |
| A nonce cannot authorize two synthetic handoffs | Concurrent same-fixture handoffs produce one Python start; second receives replay; reservation is persisted before fork | Local ledger can be rolled back/replaced by same-UID actor; no production storage or power-loss campaign |
| Ambiguous reservation is never made available automatically | Post-reservation handoff failures retain RESERVED; failed consume persistence reports UNKNOWN_AFTER_FAILURE; retry is denied as replay | External rollback of the ledger image is possible |
| Errors do not enable a permissive fallback | Faults in descriptors, seals, runtime result, timeout, and persistence return non-success and retain/mark nonce state | Tested fault set is not exhaustive against kernel/runtime faults |
| Operational bootstrap remains verify-only | Release CMake build has no test macros; production test rejects `--test-handoff` and arbitrary execute flags | Authenticity of the built executable itself is not externally established |

## 3. Files changed

- Modified `qualification/alpha_m1_collector_v2/native/verified_bootstrap.cpp`.
- Modified `qualification/alpha_m1_collector_v2/tests/alpha_m1_native_gate_test.py`.
- Updated `qualification/alpha_m1_collector_v2/native/README.md`.
- Added this report.

No Python collector source, promoted preflight component, historical corpus,
administrative path, service, mount, snapshot, or evidence file was changed.
No digest was promoted.

## 4. Tests and build

Command:

```text
python3 -m unittest -v qualification.alpha_m1_collector_v2.tests.alpha_m1_native_gate_test
```

Result after the final code change: **52 tests PASS**. The suite compiles
temporary production, Session 61 test-only, and Session 62 test-handoff
binaries with strict warnings. A separate CMake Release build of the
production target also configured and built successfully in
`/tmp/alpha-m1-session62-production-build`.

Covered areas include the shared AAP-v1 gate regressions; candidate and
manifest tampering; symlink, hard-link and FIFO rejection; mutation and
pathname replacement; size bounds; environment contamination; fixed import
policy; memfd short writes, missing/incomplete seals, closed and unexpected
descriptors; runtime result validation; runtime timeout after stdout EOF and
descendant process-group termination; nonce replay and concurrent handoffs;
crash before/after reservation; crash during handoff; and ambiguous directory
`fsync` during consumption. The latter reports `UNKNOWN_AFTER_FAILURE`, and a
subsequent attempt is denied as replay.

No CTest, privileged test, real collector, real service/journal/process query,
installation, or operational authorization was run. Power-loss behavior,
`SIGKILL` behavior for an uninterruptible process, and hostile kernel/runtime
faults remain unqualified. The timeout cleanup test establishes bounded
parent behavior for its synthetic process tree, not guaranteed termination of
every possible descendant.

## 5. Review dispositions

- **Core: PASS.** Review found no remaining concrete state-transition or
  runtime-result issue after the phase-aware reservation/consumption reporting
  and malformed-runtime-result test were added.
- **Security: PASS WITH CONDITIONS.** Descriptor handoff, memfd seals,
  application source digest checks, fixed import policy, replay behavior, and
  test/production separation were reviewed. The bootstrap/interpreter/runtime
  trust boundary remains incomplete, and the local ledger remains rollbackable.
- **Operations: PASS WITH CONDITIONS.** The review-requested stdout-EOF
  timeout handling, process-group termination, startup handshake, bounded
  cleanup wait, `/dev/null` stderr, and nonce ambiguity reporting are present.
  Process cleanup cannot be guaranteed for an uninterruptible task; this is
  surfaced as incomplete cleanup where observable.

These were source reviews of the candidate, not independent cryptographic
audits or operational qualification.

## 6. Trusted computing base and limits

The byte-continuity claim is limited to the application source files handed
through sealed descriptors in this synthetic harness. The following are not
authenticated by the Python candidate manifest: native bootstrap/test binary,
compiler/linker/toolchain, OpenSSL/libcrypto, libc, ELF dynamic loader, kernel,
the Python interpreter, standard library, and any native Python extensions.
`fexecve` pins the opened interpreter file against pathname replacement for
that call but does not establish its digest or provenance. `-I -S -B`, an
empty `sys.path`, and the import hook do not attest the runtime or its native
dependencies. `LD_PRELOAD` and other dynamic-loader influences may act before
the native program reaches `main()`; clearing child environment variables
cannot undo that. The test build does not provide a cgroup, namespace,
seccomp, or hostile-same-UID isolation boundary.

AAP-v1 fixture keys and the compiled public test root are synthetic. The
ledger's local atomic write/`fsync` sequence is exercised with temporary
fixtures, but an actor controlling the same UID/storage can restore an older
valid ledger. No monotonic anti-rollback anchor, power-loss test, production
trust-store provisioning, revocation epoch, or authenticated target provider
exists here. The synthetic reservation/consumption code is not an operational
authorization mechanism.

## 7. Baseline and historical evidence

The Session 53 report recorded a local PASS for the 149-file historical
corpus and aggregate tree SHA-256
`f229f1b367f31053a283f564b3d2fa3edd713bfa1ede5134c899466f1bb8f67f`. Session
62 did not access or reverify that corpus. Historical campaign totals remain
109 PASS, 34 BLOCKED, and 7 NOT RUN; this session closes none of those controls.
The five promoted preflight digests and the historical evidence corpus were
not changed. Their integrity was not reverified in this session.

## 8. Candidate digests

The values identify this unpromoted local candidate snapshot only. The report
digest is returned separately to avoid a self-referential value.

| Artifact | SHA-256 |
|---|---|
| `qualification/alpha_m1_collector_v2/native/verified_bootstrap.cpp` | `1f4722869ac7989c66cfe0078fb413e52e4afc8bcf85b042f5375d035e53b146` |
| `qualification/alpha_m1_collector_v2/tests/alpha_m1_native_gate_test.py` | `377335e88b3ac3e00856faa9aa32b953d950de0bc3fc68df9a1e8cf59a82c877` |
| `qualification/alpha_m1_collector_v2/native/README.md` | `55e8a6d0bb09efda45e01c2fb65a1cf3d446905d6efbd2ce0a02d586dac4bec7` |
| `/tmp/alpha-m1-session62-production-build/alpha_m1_native_verified_bootstrap` | `11050b06577a97135ac0015086de4b3cf61a6edd9509f2b539c5800b380eb265` |

## 9. Next step and status

Before considering any operational handoff, a separately authorized design
must authenticate the native bootstrap and runtime, define trust-root and
revocation provisioning, address ledger rollback, and establish OS-level
isolation and a reviewed production read set. The production target must
remain verify-only until those gates and a later explicit human authorization
are complete. Session 62 does not authorize Session 63 work or diagnostic
acquisition.

**Alpha M1 status: BLOCKED.**
