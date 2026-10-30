# Alpha Authorization Protocol v1 (AAP-v1)

**Status:** unpromoted candidate protocol; synthetic verification only.
AAP-v1 defines shared Python/C++ bytes for the candidate verifier. It does not
authorize collector execution or create human credentials.

## 1. Wire encoding

Every signed message is one JSON object encoded as ASCII bytes with **no BOM,
no leading/trailing whitespace, and no final newline**. Object keys are sorted
by ascending ASCII byte order. Objects and arrays use compact separators `,`
and `:`. Integers use minimal base-10 notation and must fit signed 64-bit
`[-2^63, 2^63-1]`; negative zero, leading zeroes, fractions, and exponents are
rejected. Protocol numeric fields require exact integer types; JSON booleans
are not integers. Strings are printable ASCII bytes `0x20..0x7e`; Unicode,
normalization, control characters, and `\u` escapes are rejected. The only
string escapes accepted in canonical bytes are `\"` and `\\`. A slash is
unescaped. Duplicate members, unknown members, unknown kinds, and unknown
versions are rejected. Array order is meaningful and fixed where a schema
defines an order.

The Python codec produces this representation and accepts input only when
parsing and re-encoding yields byte-for-byte identical input. The C++ codec
uses the same rule. No Unicode normalization or other semantic rewriting is
performed.

## 2. Signature construction

Only Ed25519 is allowed. Public keys are raw 32-byte values represented as 64
lowercase hexadecimal characters; signatures are raw 64-byte values
represented as 128 lowercase hexadecimal characters. Private signing keys
belong to an external approval service and are never embedded in the
candidate. Synthetic tests create keys in temporary directories only.

For message kind `K` and unsigned object `U` (the complete object with only
`signature` removed), the exact signed byte sequence is:

```text
ASCII("MONAD-AAP-v1") || 0x00 || ASCII(K) || 0x00 || canonical_json(U)
```

The `algorithm` member must be exactly `Ed25519`; `protocol_version` exactly
`AAP-v1`; `schema_version` exactly integer `1`. These fields are themselves
inside the signed object. Domain separation prevents a signature for one kind
from verifying as another.

## 3. Trust store

The root-signed trust-store object has exactly:

```json
{"algorithm":"Ed25519","keys":[...],"kind":"trust-store","protocol_version":"AAP-v1","schema_version":1,"signature":"..."}
```

Each key record has exactly `key_id`, `principal_id`, `role`,
`public_key_hex`, `not_before`, `not_after`. The store contains one key for
each role: `candidate-approver`, `read-set-approver`, `execution-approver`,
and `operator-target-attestor`. Key IDs, principals, roles, and public keys
are unique. Validity endpoints are integers in `[0, 4102444800]`, with
`not_before < not_after`; current time must be inside the half-open interval.
The current native root is an RFC 8032 **public test vector**, not a
deployment trust anchor. A prior root-signed store can be replayed because
there is no monotonic trust-store epoch or revocation service yet.

## 4. Signed authorization messages

Candidate, read-set, and execution messages have exactly these fields:

`algorithm, schema_version, protocol_version, kind, key_id, approver_id,
issued_at, expires_at, nonce, candidate_sha256, target_id, modules, budgets,
signature`.

Kinds are respectively `candidate-identity`, `read-set`, and `execution`.
Candidate approval must contain empty `modules` and `budgets`. Read-set and
execution must each contain the exact ordered module list
`["identity","systemd-state","process-cgroups"]` and every exact candidate
hard-budget field/value. No widening, subset reinterpretation, or defaulting
is allowed. Every budget value is an exact JSON integer; booleans are rejected
even though Python compares `true` equal to integer `1`. All three messages bind the SHA-256 of the exact candidate
manifest bytes, target ID, and same nonce. Principals must be distinct.

`issued_at` and `expires_at` are exact integers in `[0, 4102444800]`,
`issued_at < expires_at`, lifetime at most 300 seconds, `issued_at <= now`,
and `expires_at > now`. Nonces are 22–96 ASCII token characters. Target and
principal IDs are bounded ASCII tokens. There is no implied approval field:
a valid signature from the required role over the exact message is the
synthetic decision.

Target attestation has exactly:

`algorithm, schema_version, protocol_version, kind, key_id, operator_id,
candidate_sha256, request_nonce, target_id, declared_identity,
observed_identity, provenance, evidence_ref, issued_at, expires_at,
snapshot_ref, signature`.

It uses kind `target-attestation`, role `operator-target-attestor`, binds the
candidate digest and common nonce, and requires exact equality of declared and
observed identity. Identity has exactly `target_id, machine_id, hostname,
boot_id, snapshot_id`; machine ID is 32 lowercase hex characters, boot ID is
36 lowercase UUID-shaped hex/hyphen characters, other values are bounded
ASCII tokens. Provenance is exactly `synthetic-provider-v1`, and
`snapshot_ref` equals the identity's snapshot ID. These are signed synthetic
assertions, not hardware or guest attestation.

## 5. Durable synthetic reservation ledger

Ledger document has exactly `schema_version, protocol_version, ledger_id,
records, sha256`; schema is integer `2`, protocol `AAP-v1`, and ledger ID is
fixed to `alpha-m1-synthetic-ledger-v1`. `sha256` is lowercase SHA-256 of the
canonical object with the `sha256` member removed. This checksum detects
accidental corruption, **not malicious ledger edits**.

An absent nonce record means `AVAILABLE`. A ledger is bounded to 4 MiB and
4096 records; parser and writer limits match, with a bounded node budget.
Reservation at record capacity fails without modifying the ledger. Each record has exactly
`authorization_sha256, candidate_sha256, ledger_id, nonce, protocol_version,
reservation_id, state, target_id`. State is `RESERVED` or `CONSUMED`; there is
no automatic transition back to `AVAILABLE`. `INCONCLUSIVE` is a fail-closed
recovery observation for corrupt, missing, or partial persistent state; it is
not written into an untrusted or ambiguous ledger because that could overwrite
evidence needed for recovery. Reservation identity binds
candidate digest, target, digest of the four authorization signatures, nonce,
protocol/ledger identity, and unique reservation ID. A fixed exclusive lock
serializes test-only mutations. Staging file creation is exclusive; write,
file `fsync`, same-directory rename and directory `fsync` precede reporting
`RESERVED` or `CONSUMED`. A leftover staging file or ambiguous state is
`INCONCLUSIVE` and is never repaired automatically.

Production remains verify-only and does not reserve or consume. The
`SESSION61_TESTING` binary alone exposes synthetic reservation and consumption
commands. `RESERVED` is not permission to launch, and `CONSUMED` is a synthetic
ledger state, not an executed task. A same-UID attacker can roll back or
rewrite local storage; storage rollback resistance is explicitly absent.

## 6. Cross-language compatibility

`aap_v1.py` and the C++ bootstrap implement the same byte encoding, signing
domain, strict schema fields, Ed25519 verification, time bounds, role binding,
candidate/target/nonce binding, module scope, and budgets. Shared fixtures are
created using Python canonical serialization and synthetic Ed25519 keys; the
native gate accepts those messages. A test-only C++ canonicalizer emits bytes
that Python parses, hashes and verifies after a synthetic fixture signature.
The Python bundle verifier and C++ gate return matching `VERIFIED` decisions
for the same fixture. This does not authenticate either implementation, its
root, OpenSSL/libcrypto, interpreter, compiler, OS, or any operator identity.
