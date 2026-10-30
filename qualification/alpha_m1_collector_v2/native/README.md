# Session 61 AAP-v1 native gate candidate

The production CMake target remains verification-only. The only production command is:

```text
alpha_m1_native_verified_bootstrap --verify-only ABSOLUTE_SYNTHETIC_ROOT
```

It never launches Python, a collector, a shell, or another process, and it
never mutates the nonce ledger. A `VERIFIED` result means the shared AAP-v1
synthetic authorization checks passed. It is not `RESERVED`, not an execution
permit, and not an acquisition authorization.

Session 62 adds a separate test-only handoff implementation behind
`SESSION62_TESTING`. The test harness compiles that binary directly with
`SESSION61_TESTING`, `SESSION62_TESTING`, and a build-time fixed Python
interpreter path. Its only accepted extra operation is `--test-handoff` on
synthetic fixture directories. It reserves a synthetic nonce and runs one
fixed, digest-pinned harmless entry point from sealed memfds. It does not load
the collector or any diagnostic adapter. The production CMake target defines
neither test macro and rejects `--test-handoff`; the test implementation is
not a production mode or permission to collect data.

## Build and tests

Requires CMake 3.20+, a C++20 compiler, and OpenSSL 3 Crypto. Build in a fresh
temporary directory outside the repository. Run the synthetic suite with:

```text
python3 -m unittest -v qualification.alpha_m1_collector_v2.tests.alpha_m1_native_gate_test
```

The suite compiles three local binaries: production verify-only, a separate
binary with `SESSION61_TESTING`, and a separate handoff-test binary with both
test macros and a fixed interpreter path. The test binaries enable synthetic
serialization, ledger mutation, handoff, and fault injection. Test keys,
source copies, marker paths, and ledger fixtures live only in temporary
directories. Those flags do not exist in the CMake production target.

## AAP-v1

The Python codec is `qualification/alpha_m1_collector_v2/aap_v1.py`; the
normative byte and message specification is
[`alpha-m1-aap-v1.md`](../../../docs/qualification/alpha-m1-aap-v1.md).
Messages are canonical printable-ASCII JSON, strict-schema/versioned, signed
with Ed25519, and use a kind-separated signature domain. Both implementations
enforce the same trust roles, signed time bounds, candidate-manifest digest,
target binding, common nonce, fixed modules and budgets. Python-generated
signed documents are verified by C++; a test-only C++ serializer emits bytes
that Python parses, hashes, signs with a fixture key, and verifies. The RFC
8032 root key compiled into this candidate is public test material, never a
production trust anchor. Python's verifier receives a root public key from its
caller; neither implementation establishes that root or signer identity.

The native verifier measures the fixed Python component list, including
`aap_v1.py`, using the candidate manifest and descriptor-relative bounded
reads. Package inventory is checked. The contents of the `native/` directory,
the bootstrap binary itself, compiler, OpenSSL/libcrypto, libc, loader, kernel,
and environment are outside that Python digest and require an external trust
chain. `LD_PRELOAD` and loader variables are applied before `main()` and are
not neutralized here.

For Session 62 tests, the verifier retains the already-measured bounded bytes
in memory. The test-only path checks a fixed expected digest for the harmless
entry point and three package modules it imports, writes all 15 allowlisted
sources to memfds, applies and verifies `F_SEAL_SEAL`, `F_SEAL_SHRINK`,
`F_SEAL_GROW`, and `F_SEAL_WRITE`, and passes only those descriptors across
`fexecve`. The inline loader verifies descriptor order, identity, seals, size,
stability, and SHA-256 before importing application sources from memory via a
fixed import hook. It runs with `-I -S -B`, empty `sys.path`, bytecode disabled,
and a minimal child environment. The C++ parent closes its copies of all
source memfds immediately after `fork`; only the child retains the inherited
descriptors needed for loading. A test-only fault injection checks `/proc` to
confirm those parent copies are gone before the runtime reports success. The
parent rechecks the exact runtime result before consuming the synthetic
reservation. A reservation is never released after an error; a persistence
error during consumption is reported as `UNKNOWN_AFTER_FAILURE`.
Runtime error paths use nonblocking `waitpid(WNOHANG)` with a one-second reap
window after sending `SIGKILL`; if the direct child is still not reapable, the
result reports `runtime_cleanup_incomplete=true`. A synthetic poll-error test
exercises this bounded cleanup path. This does not qualify uninterruptible
kernel tasks or escaped descendants.

The handoff proves source-byte continuity only for this constrained test
fixture path. Python, its standard library and native extensions, the ELF
loader, libc, OpenSSL, kernel, toolchain and bootstrap executable are not
authenticated by the source manifest. `-I -S -B`, the fixed import hook, and
the reduced child environment do not attest those dependencies. The native
process itself may already have been affected by `LD_PRELOAD` or other loader
variables before `main()`. Process-group timeout cleanup is tested, but there
is no cgroup/namespace sandbox and no protection against a hostile same-UID
actor controlling the interpreter, ledger storage, or parent environment.
These limits keep all launch behavior test-only and Alpha M1 BLOCKED.

## Reservation ledger

The version 2 ledger fixes its protocol and ledger IDs, carries candidate,
target and authorization-signature digests per reservation, and permits only
`RESERVED` and `CONSUMED` records. It is bounded to 4 MiB and 4096 records;
native read, parser, and test writer caps agree, and reservation refuses a full
ledger without changing it. A missing nonce means `AVAILABLE`. The
test-only mutator serializes cooperating processes with `flock`, writes an
exclusive staging file, fsyncs it, renames atomically, and fsyncs the directory
before reporting a transition. It never automatically clears a reservation.
Partial staging and ambiguous recovery fail closed. Production only reads and
validates the ledger.

The SHA-256 ledger checksum detects accidental corruption, not malicious
rewriting. A same-UID actor can restore an older valid ledger or replace the
directory. Power-loss durability beyond the tested injected boundaries is not
established. The trust store also has no monotonic revocation epoch; older
root-signed stores can restore keys. Both limits must be addressed externally
before any operational use.

No diagnostic source, D6 payload, service, journal, real process, cgroup, or
administrative evidence is accessed. No digest is promoted.
