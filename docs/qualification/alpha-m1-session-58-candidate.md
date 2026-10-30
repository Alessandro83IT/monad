# Alpha M1 Session 58 — trust-chain candidate report

**Status: SESSION 58 PASS WITH CONDITIONS. Alpha M1 remains BLOCKED.** This is
an unpromoted, synthetic-only prototype. No real collector, guest service,
journal, process inventory, D6 payload, or administrative evidence was read.

## Implemented architecture

`qualification/alpha_m1_collector_v2/candidate_identity.json` is the static,
versioned candidate identity document. Its fixed component list is enforced in
code; each entry carries byte length and SHA-256, and the aggregate uses
length-framed paths, sizes, and raw SHA-256 values. Its exact document bytes
must be approved by the Candidate Approver role. The entry point and maximum
read set/budget are code-fixed. The manifest is not itself a signature or root
of trust.

`trust_chain.py` parses a strict, duplicate-key-rejecting schema and accepts a
trust store only when its canonical payload authenticates under an independently
provided root anchor. Candidate, read-set, execution, and target-attestor keys
are distinct. The verifier does not provision keys or generate approvals.
Synthetic tests create ephemeral keys; no operational keys or trust store are
included.

`target_attestation.py` separates declared and observed identity, provenance,
evidence reference, operator, freshness, and snapshot reference. The only
accepted provider label is synthetic. Machine ID, host name, boot ID, and
snapshot ID are input attributes, not cryptographic proof of target identity.
The launcher uses its own wall-clock reading; system time remains an
environmental dependency and is not a trusted time source.

`nonce_ledger.py` requires a preinitialized private mode-0700 owner directory,
a private lock file, and a valid authenticated ledger. Cooperating launchers
take an exclusive `flock`, append the nonce as CONSUMED by exclusive staging
file, `fsync`, same-directory rename, and directory `fsync`; only then does
the synthetic entry point run. The nonce remains consumed after a crash before
or during execution. A post-execution update records FINISHED or FAILED.
Malformed state, stale staging, lock contention, exhaustion, and persistence
errors fail closed. A previous valid ledger image can be restored by a same-UID
actor, including its valid MAC: local storage does **not** provide anti-rollback
protection. The owner can also bypass advisory locking or replace the directory.
Production use would require a separately protected monotonic service or
hardware-backed counter and a stronger ownership boundary.

`verified_launcher.py` opens fixed component paths descriptor-relative with
no-follow flags, rejects special files and hard links, reads bounded bytes,
checks before/after metadata, and hashes the captured byte strings. It checks
the candidate manifest approval, the target attestation, the exact maximum
module and budget approval, and the separately signed execution approval.
Read-set and execution approver identities and keys must differ. Only after all
checks does it consume the nonce. It compiles the retained bytes of the one
fixed, import-free `synthetic_entry.py` and executes with empty builtins. It
accepts only the fixed harmless return value. `operational=True` always fails
closed; there is no real collector path.

## Trust and execution limits

This remains a Python library prototype, not a verified launcher. Python has
already imported and executed the launcher and its package dependencies before
`launch_synthetic` can verify candidate component bytes. The interpreter,
standard library, initial import path, process environment, and root-anchor
provisioning are outside this candidate's measured chain. Python-level
`compile`/`exec` of retained bytes avoids reopening the entry point and removes
imports from its restricted builtins, but it does not verify the interpreter
or provide an OS isolation boundary. The operational path is therefore disabled
instead of claiming same-byte trust for a real collector. A production design
needs a separately reviewed native/minimal bootstrap or another independently
verified execution boundary before enabling any diagnostic module.

The ledger's atomic replace handles cooperating process crashes but depends on
filesystem semantics for ext4-like local filesystems; this session did not
qualify power-loss behavior, storage controllers, network filesystems, or
directory replacement by the same UID. Approval expiry uses local wall time.
The trust store uses HMAC as a testable role-separation contract, not a
non-repudiable human signature scheme; future deployment needs a reviewed
public-key signature format and authenticated human identity workflow.
The store has no monotonic epoch or revocation ledger: replaying an older
root-authenticated store can restore a revoked role key. Revocation rollback
must be solved by a protected monotonic version or an online trust service
before any deployment.

## Test and review record

The final targeted run passed **46 tests**: 17 Session 58 trust-chain tests and
29 Session 56/57 core tests, using only synthetic temporary fixtures. CTest and
any real collector path were not run.

Synthetic tests in `tests/alpha_m1_collector_v2_trust_test.py` cover valid
single-use launch, replay, component mutation, manifest/entry-point alteration,
symlink and hard-link rejection, trust-store tampering, role/budget/scope
mismatch, target mismatch and expiry, forged approval, ledger corruption,
concurrent launch attempts, restoration of an older ledger snapshot, invalid
lock mode, lock timeout, descriptor cleanup after unlock failure, missing
trust store, staging-write failure, rename failure, and stale-stage refusal.
The rollback test is intentionally a demonstrated limitation, not a passing
anti-rollback claim. These tests fault-inject the write and rename boundaries;
they do not establish power-loss durability on the target filesystem.

Independent Core, Security, and Operations reviews were completed. Core found
untyped approval budgets, missing human-principal binding, and descriptor
cleanup gaps; these were fixed and covered by tests. Security confirmed those
fixes and found no additional high/medium flaw for the synthetic-only scope.
Security also confirmed the explicit operational limits: Python cannot verify
its own bootstrap before import, the root anchor is supplied by the calling
environment, and the local ledger/trust-store version have no rollback-proof
revocation state. Operations confirmed the setup cleanup and noted that
power-loss semantics, capacity recovery, and same-UID owner attacks remain
unqualified. No operational authorization was created, no digest was
promoted, and no historical evidence was rewritten.

## Qualification impact and next steps

All five previously promoted preflight digests remain outside this candidate
and were checked unchanged. Four file digests matched their pins; the inline
loader test constant and the documented Session 48 command were extracted
statically and matched byte-for-byte at SHA-256
`feef002511128263daf5fa05fcaa72686a688d58fb717b2f8d1a9d026b205021`. The
fixed historical tree verifier
`qualification/alpha-m1-evidence-tree-digest.py` was reviewed statically
(SHA-256 `b003d2b02dcbb02d304371524ef395838599fd918e1505bd100fc2a1340435e3`)
and run only against its fixed authorized evidence root. It returned 149 files
and tree SHA-256
`f229f1b367f31053a283f564b3d2fa3edd713bfa1ede5134c899466f1bb8f67f`
(PASS local). The verifier rejects symlinks/special files and detects observed
concurrent changes. This is a local integrity check, not guest identity or
external attestation; no historical file was modified.

Before any later operational design can be considered, the project must:

1. Resolve Python self-verification with an independently verified bootstrap.
2. Select a production signature and operator identity system, provision a
   separately controlled root, and define revocation and trusted time.
3. Replace the local rollbackable nonce ledger or explicitly accept and bound
   its rollback threat; qualify crash/power-loss semantics.
4. Obtain distinct human decisions for candidate identity, read set, target,
   and execution, then conduct a new security and operations review.
5. Keep diagnostic execution disabled until the read set is approved and
   environment-specific acquisition gates are completed.

## Artifact integrity

The current per-file SHA-256 values are listed in the Session 58 completion
response. `candidate_identity.json` covers the 14 code components enumerated by
`candidate_identity.AUTHORIZED_COMPONENT_PATHS`; it is intentionally unsigned
in-tree and needs an external Candidate Approver signature. The manifest must
be regenerated and independently reviewed after any source change. No hash is
promoted by this report.
