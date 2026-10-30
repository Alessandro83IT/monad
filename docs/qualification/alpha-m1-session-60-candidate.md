# Alpha M1 Session 60 — native trust and authorization gate candidate

**Verdict: SESSION 60 PASS WITH CONDITIONS. Alpha M1 remains BLOCKED.** This
unpromoted candidate performs synthetic, verification-only preflight. It does
not execute Python, consume operational authorizations, access real diagnostic
sources, or authorize evidence acquisition.

## 1. Implemented architecture

`qualification/alpha_m1_collector_v2/native/verified_bootstrap.cpp` now
implements the Session 60 native wire contract using C++20 and OpenSSL EVP
Ed25519/SHA-256. Production accepts only
`--verify-only <absolute-candidate-root>`. It rejects all other operations.
The result uses `VERIFIED`, `DENIED`, `EXPIRED`, `REPLAY`,
`TARGET_MISMATCH`, `CANDIDATE_MISMATCH`, `POLICY_ERROR`, and `INCONCLUSIVE`
as applicable. `VERIFIED` is a point-in-time synthetic preflight result only;
it is not execution approval, promotion, or diagnostic authorization.

The candidate is not integrated with, and does not invoke, the Python
`verified_launcher.py`. No Python import or process launch is present in the
native executable. The production path only inspects the ledger; it does not
reserve or consume its nonce. A separate binary compiled with
`SESSION60_TESTING` exposes test-only synthetic consumption and fault
injection. It is built by the test harness, not by the production CMake
target.

## 2. Contract-equivalence matrix

| Area | Python Session 58 implementation | Native Session 60 implementation | Assessment |
|---|---|---|---|
| Candidate identity | Manifest SHA-256 is signed; 14 component hashes and framed aggregate; fixed entry point/read-set/budget | Same manifest digest, 14 component records, aggregate framing, fixed modules/budgets | Compatible for this constrained manifest structure; native package additionally requires a `native/` directory whose contents are not hashed |
| Canonical JSON | Python `json.dumps(sort_keys=True,separators=(',', ':'),ensure_ascii=True)`; strict loader rejects duplicate keys; signed canonical payload omits signature | Parser rejects duplicate keys/non-integer values and requires byte-for-byte canonical ASCII JSON; Ed25519 signs canonical object without signature | Canonical bytes align for tested ASCII schema values; no general cross-language canonicalization proof |
| Trust store | HMAC root envelope with role -> key lists, injected root anchor | Ed25519 root signature, pinned RFC 8032 public test vector, exactly one unique key per role with time window | Not wire-compatible; native root is synthetic and not operationally trusted |
| Role signatures | HMAC-SHA256 with injected symmetric keys | Ed25519 role public keys authenticated by root signature | Not wire-compatible; native cryptographic signatures provide no proof of human identity or custody |
| Candidate/read-set/execution | Python launcher `Approval` fields; read-set and execution share nonce; candidate approval uses its own path; decision/approver metadata as implemented in v2 | Candidate, read-set and execution documents use one shared nonce, manifest SHA-256, target, modules/budgets, key/principal and validity; role signature implies approval | Semantics overlap but schemas and separation semantics diverge; do not reuse one implementation's documents in the other |
| Target attestation | Python `VerifiedTarget` signs declared/observed identity, synthetic provenance, operator, freshness and snapshot; the target structure does not bind candidate digest/request nonce | Native target document binds both identities, candidate manifest SHA-256, common nonce, target, attestor/operator, provenance, evidence reference, snapshot and freshness | Native binding is stronger in this dimension; still a synthetic signed assertion, not independent target evidence |
| Nonce | Python HMAC-authenticated local ledger consumes before fixed synthetic entry; rollbackable | Production checks a checksum-protected local ledger but does not consume; test build can record `CONSUMED_TEST_ONLY` | Not transactionally equivalent; native verification is not one-use authorization |
| Output/state | Python launcher returns synthetic-entry result and ledger finish state | Native returns canonical JSON verification decision; `python_started=false` and `nonce_consumed=false` on normal verify | Deliberately no execution equivalence |

The native contract intentionally uses one common nonce across the three
approvals and target attestation; the Python `authorization.py` data classes
have separately identified approval records/nonces, while
`verified_launcher.py` requires a common read-set/execution nonce and has a
separate target object. Python and C++ use distinct trust roots, signatures,
field names and parsers. The test harness creates Python canonical JSON bytes
and native Ed25519 signatures and confirms native acceptance for its narrow
fixture schema; it does **not** establish semantic equivalence with Python
trust or authorization decisions. A future shared protocol needs a new
versioned specification, shared positive/negative fixtures, and independent
differential review. No Python contract was silently changed to match native.

## 3. Trust store, authorization and target contracts

The native trust store is schema 1, policy `alpha-m1-native-authz-v1`, and is
root-signed. It lists exactly one currently valid key for each role:
Candidate Approver, Read-set Approver, Execution Approver, and
Operator/Target Attestor. Role, principal, key ID, public key and validity are
checked; roles, key IDs and key material are unique. The embedded root public
key is the RFC 8032 test vector. Its private seed appears only in the
synthetic test helper. Neither this root nor the test key model is suitable
for deployment. A compiled root has value only after an external party
authenticates the bootstrap binary and its launch environment.

Separate signed documents must bind the measured manifest SHA-256, target ID,
common nonce, role/principal, issue/expiry and policy. The candidate approval
grants no modules or budgets. The other approvals must exactly match the
code-fixed module list (`identity`, `systemd-state`, `process-cgroups`) and
all fixed budget values. Candidate, read-set, execution, and target-attestor
principals are separated. Missing, altered, expired, wrong-role, target- or
candidate-mismatched, scope-expanded, budget-expanded, duplicate-key,
non-canonical, or invalidly signed documents fail closed.

The signed synthetic target assertion distinguishes declared and observed
identity and binds both to candidate digest and nonce. It also includes
synthetic provenance, evidence reference, operator, freshness, and snapshot
reference. Hostname, machine ID, boot ID and snapshot label remain attributes
asserted by the signer, not cryptographic proof that the intended VM was
observed. Native expiry checks use the local wall clock; trusted time and
revocation/epoch rollback protection are not implemented. The trust store has
no monotonic epoch or revocation state: restoring an older root-signed store
can re-enable a role key that remains within its validity interval. A
protected monotonic policy version or online revocation authority is required
before deployment.

## 4. Ledger semantics and failure boundaries

The gate opens `.alpha_m1_native_gate/nonce-ledger/` and its fixed lock and
ledger files without following symlinks, checks owner-only directory/file
properties, takes a bounded exclusive `flock`, validates canonical JSON and
the ledger SHA-256 checksum, and rejects malformed, duplicate, reserved or
consumed nonce state. Known replay outcomes report `REPLAY`; a clean ledger
yields `VERIFIED_NOT_CONSUMED`. It is a
read-only check in production.

The `SESSION60_TESTING`-only path reopens and locks the synthetic ledger,
records `CONSUMED_TEST_ONLY` through an exclusive staging file, write loop,
file `fsync`, same-directory rename and directory `fsync`. It exists to test
replay/persistence behavior and is not an operational nonce transaction. If
an error happens after rename before directory `fsync`, the reported state is
`UNKNOWN_AFTER_FAILURE`; the fixture must be inspected before retrying.
Concurrent test consumption produces one winner and one replay denial. A
same-UID actor can restore an earlier valid ledger image or replace its
directory; power-loss durability and filesystem/controller behavior are not
qualified. Advisory locking coordinates only cooperating processes.

## 5. Files changed or added

- Modified `qualification/alpha_m1_collector_v2/native/verified_bootstrap.cpp`.
- Modified `qualification/alpha_m1_collector_v2/native/README.md`.
- Added `qualification/alpha_m1_collector_v2/tests/alpha_m1_native_gate_test.py`.
- Added this qualification report.

No Python collector component, promoted preflight component, historical
corpus, evidence file, service, mount, snapshot, or administrative path was
changed. The legacy Session 59 report remains a historical record of its
then-current HMAC prototype and is superseded for the current native source by
this report and README.

## 6. Qualification performed

The test harness builds strict-warning C++20 binaries in a temporary
directory: production verify-only and a distinct test-only-consumption binary.
All keys, candidate copies, authorization documents, ledger records, and
target identities are synthetic. No real collector, Python launcher,
systemd, journal, process, cgroup, D6 payload or administrative evidence was
read.

| Test area | Result | Coverage / limit |
|---|---|---|
| Strict native build | PASS | `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror`; OpenSSL Crypto |
| Valid synthetic authorization chain | PASS | Result is verify-only; no Python and no production nonce consumption |
| Root/role/approval/timing | PASS | Altered root signature, missing execution approval, wrong role, expired approval, altered candidate digest |
| Scope and budgets | PASS | Expanded module and budget fixtures rejected |
| Target and candidate | PASS | Target mismatch, changed component, added component rejected |
| Parser/filesystem | PASS | Duplicate JSON keys, non-canonical JSON, symlink, hard link, FIFO, corruption rejected |
| Role separation and target/candidate binding | PASS | Coincident approver principals, target mismatch, signed extreme timestamps, candidate digest mismatch rejected |
| Ledger replay/concurrency/rollback | PASS (rollback limitation demonstrated) | Test-only synthetic consumption and two concurrent contenders: exactly one success, one replay; same-UID restoration of the old ledger image permits replay |
| Concurrent production verification | PASS (inert semantics only) | Both verify-only calls can return VERIFIED_NOT_CONSUMED; this is not one-use authorization |
| Persistence fault injection | PASS | ENOSPC, file fsync EIO, rename, directory fsync EIO and stale stage return non-VERIFIED; short write and injected EINTR complete through retry loops |
| Arbitrary operation | PASS | `--execute` rejected; result does not start Python |
| Real-system behavior and power-loss durability | NOT RUN | Prohibited/outside synthetic scope |
| Cross-implementation authorization equivalence | NOT DEMONSTRATED | Wire formats and crypto contracts diverge; only constrained canonical fixture acceptance was exercised |
| TOCTOU races under hostile same-UID mutation | NOT DEMONSTRATED | Descriptor/no-follow/stability checks exist; same-UID control and concurrent replacement are not eliminated |

The current suite has 23 passing test methods; several methods include
multiple fault variants. ENOSPC/EIO/rename/stale-stage are injected at the
test-only write path. Short read, live rename races during component hashing,
permission denial, process-kill at every persistence boundary, and actual
power-loss recovery were not established. They remain explicit gaps rather
than inferred passes.

## 7. Review status

Independent Core review confirmed the timestamp overflow fix, the aggregate
fixture's use of the official Python framing function, and corrected README
claims. Core found no remaining blocker and noted only a path typo in the
artifact table, now corrected. Security identified the same timestamp-overflow
issue independently; the bounded timestamp checks and extreme signed fixture
close it. Security also identified imprecise nonce-state output on known replay
branches and missing trust-store rollback wording; replay now reports
`REPLAY`, and the trust-store epoch/revocation limitation is documented.
Security confirmed the test root, native binary/toolchain, loader
environment, and local rollback limits remain operational trust blockers.
It also prompted the known-replay state correction, now covered by assertions.
Operations review of the candidate identified that production verification
does not consume a nonce, that build dependencies are ambient, and that test
compiler output should be cleaned up. We added tests proving the first is
inert (two verify-only calls both return VERIFIED_NOT_CONSUMED) and now clean
only each test run's own temporary build directory. The Operations reviewer
confirmed the final nonce-state distinction and temporary-directory handling
from the described diff. A fresh CMake Release build succeeded and is retained
at `/tmp/alpha-m1-session60-final-cmake-f4iuc26b` for inspection. No reviewer
approved operational use.

## 8. Properties demonstrated and not demonstrated

Demonstrated in synthetic tests: exact top-level candidate package inventory
and measured digests for its 14 authorized Python components; strict
constrained document parsing; verification of the
root and four role signatures; exact scope/budget checks; target/candidate/
nonce binding; expiry; private descriptor-relative ledger checks; replay
detection; test-only single-winner consumption; and fail-closed responses to
the listed persistence faults.

Not demonstrated: authenticity of this bootstrap binary or embedded root;
human identity/key custody; trusted wall clock; revocation and monotonic
authorization epoch; guest identity attestation; hostile same-UID isolation;
anti-rollback or power-loss durability; shared Python/C++ wire equivalence;
real acquisition read-set approval; exact bytes later executed (there is no
execution path); runtime/dependency/toolchain authenticity; or any privileged
VM campaign result. The result field `VERIFIED` cannot be used as an
operational authorization.

## 9. Baseline and historical state

Session 60 did not re-read the historical evidence corpus. The latest recorded
local verification in the Session 58 report was 149 files with aggregate
SHA-256
`f229f1b367f31053a283f564b3d2fa3edd713bfa1ede5134c899466f1bb8f67f` (PASS
local). Historical campaign totals remain 109 PASS, 34 BLOCKED, and 7 NOT
RUN; this session supplies no new evidence to close those controls. The five
Session 49 promoted digests and the Session 58 inline loader digest were not
recomputed in this session and were not modified. No new digest is promoted.

## 10. Candidate artifact digests

Values below identify this unpromoted checkout snapshot only and are to be
recomputed if any listed file changes.

| Artifact | SHA-256 |
|---|---|
| `native/verified_bootstrap.cpp` | `0722093237211a92ab9752d9900bd3e01b18023290809d661d31c6f176e8fc68` |
| `native/CMakeLists.txt` | `f911f4f6435c223acf97ff82d6a8ecd4bb9c15048edaddccde4e3bbadb944316` |
| `native/README.md` | `38b231e70d4f5a8cadf148ab3a6f4443f60172e0e124a12f220b05f7d1ad064e` |
| `qualification/alpha_m1_collector_v2/tests/alpha_m1_native_gate_test.py` | `e42541e9e28d93e51557b3ab6804f3d207cb9e7a97bbaa54d297916e804be9cf` |
| Release executable from temporary local CMake build | `dcf1e83e900186f866c6fc44e99d9224b874e078654d6e43e7651bb0ec9497e6` |

The report's own digest is recorded in the completion response rather than
inside itself. The executable digest identifies the local build only; toolchain
and runtime dependencies were not independently authenticated.

## 11. Residual gates and Session 61 proposal

Alpha M1 remains **BLOCKED**. Before any acquisition or operational launch:

1. Define and approve a single versioned Python/C++ authorization wire
   contract and shared positive/negative differential fixtures.
2. Establish externally authenticated bootstrap identity and protected root
   provisioning, revocation, trusted time, and operator identity.
3. Decide whether to replace the local nonce ledger with a rollback-resistant
   authority; qualify crash and storage durability on the intended target.
4. Independently review same-UID threat assumptions and candidate-tree race
   handling; include concurrent replacement and interruption tests.
5. Obtain explicit human approval of the exact read set, target evidence,
   candidate digest, budgets, and any future execution authorization.
6. Complete the approved real Debian campaign and review all required cases;
   Session 60 does not reduce historical BLOCKED or NOT RUN counts.

The next useful technical increment is a protocol design/differential fixture
session that resolves Python/native incompatibility without enabling launch.
No Session 61 work or real acquisition is authorized by this report.
