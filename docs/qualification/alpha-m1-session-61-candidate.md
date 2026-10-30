# Alpha M1 Session 61 — AAP-v1 and durable reservation candidate

**Disposition:** `SESSION 61 PASS WITH CONDITIONS`  
**Operational status:** Alpha M1 remains **BLOCKED**.  
**Scope:** synthetic protocol, parser, signature, and test-only ledger work. No
collector, Python launcher, diagnostic source, real authorization, or operational
reservation was invoked.

## 1. Architecture and boundary

AAP-v1 is the shared byte protocol for the new Python verifier in
`qualification/alpha_m1_collector_v2/aap_v1.py` and the C++20 native verifier in
`qualification/alpha_m1_collector_v2/native/verified_bootstrap.cpp`. The wire
format is strict printable-ASCII canonical JSON, with duplicate/unknown fields
rejected, bounded signed 64-bit integer parsing, exact schema types, Ed25519
signatures, and a kind-specific signature domain. Candidate/read-set/execution
approvals and target attestations bind the same candidate manifest digest,
target, nonce, fixed modules, and fixed budget policy.

The native production target only supports `--verify-only`. It returns
`VERIFIED` only for the synthetic gate checks; it does not start Python, invoke
the collector, or mutate the ledger. Test-only builds expose serialization and
synthetic reservation/consumption operations behind `SESSION61_TESTING`.
`RESERVED` and `CONSUMED` are test ledger states, never execution grants.

The legacy Session 58 Python trust chain remains a separate historical
contract: it used HMAC-oriented role contracts, different message schemas and
target-binding semantics. It is not wire-compatible with AAP-v1. Session 61
does not claim to migrate or integrate that old launch path. AAP-v1 Python and
native verification are cross-tested against the same fixtures; production
continues to have no handoff path.

## 2. Compatibility matrix

| Comparison | Semantic compatibility | Byte/wire compatibility | Evidence |
|---|---|---|---|
| Session 58 Python contracts vs Session 60 native contract | Partial overlap in candidate, roles, target, expiry and nonce concepts; schemas/bindings differ | **Incompatible**: legacy HMAC/schema versus native Ed25519/schema | Session 60 report and static source review |
| AAP-v1 Python vs current native C++ | Matching fixed schema, role separation, bindings, exact types, time/scope/budget rules | **Compatible for AAP-v1 fixtures** | Python-signed fixture accepted by C++; C++ canonical bytes parsed, hashed and signature-verified by Python; decision parity asserted |
| AAP-v1 arbitrary JSON implementations | Not claimed | Not claimed beyond the specified bounded ASCII profile | Strict schema/size/depth/node limits; no general JSON interoperability claim |
| Verification vs reservation/launch | Different state transitions | Not applicable | Tests assert `VERIFIED` leaves nonce `AVAILABLE`; only test binary can persist `RESERVED`/`CONSUMED`; no launch code exists |

Python's `True == 1` equality behavior was explicitly closed for candidate
schema versions and signed/request budget values with exact integer type checks.
Native ledger parsing now requires unique `res-` plus 32 lowercase hexadecimal
reservation IDs, matching Python. A 4096-record ledger fits the shared 4 MiB
read/write limit and native parser node budget. The writer refuses a new
reservation when the record cap is already full.

## 3. AAP-v1 and reservation contracts

The normative specification is [alpha-m1-aap-v1.md](alpha-m1-aap-v1.md).
Ed25519 private signing keys are external to the candidate; tests use only
deterministic fixture keys in temporary directories. The embedded native root
is an RFC 8032 public test vector, not an operational trust anchor. Signatures
are over `MONAD-AAP-v1 || NUL || kind || NUL || canonical(unsigned object)`.
Unknown algorithms, versions, roles, duplicate fields, ambiguous encodings,
expired signatures, target/candidate mismatch, expanded scope, and non-exact
budgets fail closed.

The synthetic ledger has fixed protocol/ledger identities, checksum validation,
4096-record and 4 MiB caps, unique nonce and reservation identifiers, and only
`RESERVED`/`CONSUMED` stored states. Missing nonce is `AVAILABLE`.
`INCONCLUSIVE` is a fail-closed observation for a corrupt, ambiguous, or
unreadable ledger, not an automatic rewrite. Reservation holds a cooperative
exclusive lock, writes an exclusive same-directory stage, fsyncs the file,
renames, and fsyncs the directory before reporting success. The test mutator
refuses capacity overflow without modifying the ledger. Production remains
read-only with respect to the ledger.

The checksum detects accidental corruption, not malicious changes. A same-UID
actor can replace or roll back the ledger. Power-loss guarantees beyond process
crash/fault-injection boundaries are unproven. There is no monotonic trust-store
epoch or revocation mechanism.

For test-only mutation failures, `UNKNOWN_AFTER_FAILURE` is a conservative
generic response. It also covers known pre-write rejection such as a full
ledger, where the fixture confirms no mutation occurred; this is fail-closed
but less diagnostically precise than a dedicated capacity status.

## 4. Files changed or added

- Updated candidate codec: `qualification/alpha_m1_collector_v2/aap_v1.py`.
- Updated exact candidate schema check and regenerated manifest:
  `qualification/alpha_m1_collector_v2/candidate_identity.py` and
  `qualification/alpha_m1_collector_v2/candidate_identity.json`.
- Updated native AAP gate, strict ledger validation, parser budget, phase-aware
  status reporting, matching 4 MiB writer cap, and full-ledger capacity guard:
  `qualification/alpha_m1_collector_v2/native/verified_bootstrap.cpp`.
- Expanded synthetic cross-language, ledger-capacity and regression tests:
  `qualification/alpha_m1_collector_v2/tests/alpha_m1_native_gate_test.py`.
- Updated protocol and native-operation documentation:
  `docs/qualification/alpha-m1-aap-v1.md` and
  `qualification/alpha_m1_collector_v2/native/README.md`.
- This report: `docs/qualification/alpha-m1-session-61-candidate.md`.

No promoted component or historical evidence file was changed. The checked-in
candidate identity manifest is a candidate declaration, not a promotion.

## 5. Qualification results

| Check | Result | Notes |
|---|---|---|
| Synthetic unit/integration/adversarial suite | **PASS — 36 tests** | `python3 -m unittest -v qualification.alpha_m1_collector_v2.tests.alpha_m1_native_gate_test` |
| Python/C++ shared-fixture verification | **PASS** | Both verifiers accept the same signed fixture and return matching decision/target; C++ canonical output is parsed and verified by Python |
| Maximum ledger parser fixture | **PASS** | Both implementations handle 4096 records; native reports replay for the included nonce |
| Maximum ledger persistence fixture | **PASS** | Test-only reservation persists record 4096, output exceeds 1 MiB, and Python parses it |
| Full-ledger reservation | **PASS** | 4097th record denied as inconclusive and ledger bytes unchanged |
| Fault injection | **PASS** | Short write, EINTR, ENOSPC, EIO/fsync, rename/stale stage, crash boundaries, corruption, interruption recovery, concurrency and same-UID rollback cases fail closed or demonstrate documented limits |
| Production CMake Release build | **PASS** | Fresh out-of-tree build in `/tmp/alpha-m1-session61-final-build`; GCC 14.2.0 and OpenSSL 3.5.7 |
| Real collector / Python launch / diagnostic access | **NOT RUN / PROHIBITED** | No real collection or runtime handoff was implemented or exercised |
| Power-loss durability / malicious same-UID rollback resistance | **NOT DEMONSTRATED** | Outside what process-level temporary fixtures can establish |

An earlier regression run exposed malformed-ledger errors escaping as generic
parser errors and being reported with the wrong nonce phase. The path now maps
all ledger validation failures to `INCONCLUSIVE`, while pre-ledger trust
failures report `NOT_CHECKED`; dedicated tests cover both. The Python boolean
budget divergence and the ledger writer/parser capacity mismatch were also
found in review and corrected before the final 36-test run.

## 6. Review dispositions

- **Core:** PASS after corrections. Cross-language schemas, exact budget types,
  ledger reservation-ID uniqueness, node/byte bounds and full-capacity
  persistence were checked against the implementation and fixtures.
- **Security:** PASS WITH CONDITIONS. Domain-separated Ed25519 and strict
  parsing align for AAP-v1; exact integer checks reject booleans. Synthetic
  public root, ambient native/runtime/toolchain identity, and rollbackable
  local ledger remain trust gaps. Generic `UNKNOWN_AFTER_FAILURE` labeling on
  known no-write reservation rejections is a minor diagnostic limitation.
- **Operations:** PASS WITH CONDITIONS. Phase-specific nonce reporting,
  file/dir fsync ordering, bounded lock, crash/fault behavior and capacity
  refusal were reviewed. Fault injection does not prove power-loss behavior.

The reviews were independent source reviews, not operational qualification or
independent cryptographic implementation audits.

## 7. Properties and limits

Demonstrated for the candidate fixtures: strict canonical byte acceptance,
duplicate-field rejection, Ed25519 signature verification, fixed role and
scope enforcement, target/candidate/nonce binding, Python/C++ decision parity,
unique reservation IDs, serialized test-only reservation, bounded ledger
capacity, and fail-closed reporting for tested corruption/failure cases.

Not demonstrated: authenticity of the actual bootstrap binary before `main`,
trustworthiness of the compiler/linker/OpenSSL/libc/loader/kernel, absence of
pre-main loader injection, real operator or guest identity, authenticity or
revocation freshness of an operational trust store, anti-rollback against a
same-UID actor, power-loss recovery, or any property of collector execution.
The old Session 58 launcher remains outside the AAP-v1 wire contract and is not
enabled by the native gate.

## 8. Digests and historical baseline

SHA-256 values below identify the current unpromoted candidate artifacts after
the final changes. They are local workspace observations; no digest is
promoted.

| Artifact | SHA-256 |
|---|---|
| `qualification/alpha_m1_collector_v2/aap_v1.py` | `672d493e8a9fed8b9601e3f4e6d94e06b6c92fb0e2a15e4f9ff71646850f3f05` |
| `qualification/alpha_m1_collector_v2/candidate_identity.py` | `596790aaa34353b87329b7329396378634bee0e96435bcbbfe890a003ff1e702` |
| `qualification/alpha_m1_collector_v2/candidate_identity.json` | `8f637e64153748e3291533a7e3590679f4ab01856514525fd63acd7e9f5bb77c` |
| `qualification/alpha_m1_collector_v2/native/verified_bootstrap.cpp` | `1d90c08eb5b2dbfdafbcc8459421fbecff45051d10991d71c00b666c61462d8b` |
| `qualification/alpha_m1_collector_v2/tests/alpha_m1_native_gate_test.py` | `5be0282fed6638f89687c7ac9461d8c3011528d01764be74abf5a02617ea0920` |
| `qualification/alpha_m1_collector_v2/native/README.md` | `d26448b2164f034972aa3e053ed1939a3750a60d23ec116c469cdd759117a530` |
| `docs/qualification/alpha-m1-aap-v1.md` | `4119c209a9ffdc84d128eabcff2510cf583659c82b14f938d4a32696af8b2117` |
| `/tmp/alpha-m1-session61-final-build/alpha_m1_native_verified_bootstrap` | `1e9846537893a8bae140ad2e6ba0baf896e1a1d276c64d94fec4324f40b5ee72` |

SHA-256 of this report is recorded in the final session response to avoid a
self-referential digest field.

The historical evidence corpus was not re-read or modified in Session 61. The
latest cited local integrity result remains Session 53: 149 regular files,
aggregate SHA-256
`f229f1b367f31053a283f564b3d2fa3edd713bfa1ede5134c899466f1bb8f67f`, no
symlink/special files, PASS local. It is not a new guest-side verification.
The historical campaign remains 150 controls: 109 PASS, 34 BLOCKED and 7 NOT
RUN; Session 61 supplies no evidence to close those controls. The five promoted
preflight digests and historical corpus are unchanged and not revalidated here.

## 9. Remaining gates and next proposal

Alpha M1 remains **BLOCKED**. Before any operational use, a later human-reviewed
work item must establish an externally authenticated bootstrap/root, a
production trust-store provisioning and revocation policy, rollback-resistant
nonce storage or an explicitly approved residual-risk policy, pinned build and
runtime dependencies, and a separately reviewed read-set/execution
authorization path. The Session 58 legacy launch path must not be treated as
AAP-v1 compatible without an explicit migration and equivalence review.

Proposed Session 62: perform a bounded design and static integration review of
trust-anchor provisioning, bootstrap binary identity and ledger rollback
mitigations; keep native execution disabled; agree human review criteria for
any future AAP-v1 promotion. No acquisition or launch is implied.
