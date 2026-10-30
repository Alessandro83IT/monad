# Alpha M1 Session 57 — Collector v2 Candidate

Status: **unpromoted synthetic candidate**. Alpha M1 remains **BLOCKED**.

This document records the Session 57 core-hardening and adapter candidate. It
does not authorize any real acquisition. No system backend or collector was
invoked, and no live service query, journal read, process enumeration,
administrative evidence read, or D6 payload read was performed. Process and
unit adapters operate only on synthetic abstract-source fixtures in tests.

## Candidate boundaries

The candidate is isolated under `qualification/alpha_m1_collector_v2/`. The
legacy collector, runner, bootstrap, CTest helper, inline loader, promoted
Session 49 digests, and historical evidence are not changed by this work. The
synthetic tests use only temporary directories and in-memory source fixtures.

Three fixed adapters are registered in code: `identity`, `systemd-state`, and
`process-cgroups`. Only an explicitly selected and doubly approved module is
dispatched. Other selected modules have no real implementation and return
`INCONCLUSIVE`; unselected modules are recorded as `NOT_REQUESTED`. There is no
dynamic plugin loading or user-configured code execution.

## Data contract and completion

Schema version 1 records candidate and acquisition identities, module scope,
authorization references, fixed budget limits, operation counters, module
statuses, output artifact digests, elapsed duration, errors, anomalies, and
proposed completion. The final manifest is deliberately written with
`completion: INCONCLUSIVE` and `finalization_state: REQUIRES_VALID_SEAL`. A
separate SHA-256 seal binds its exact byte length, digest, acquisition ID, and
proposed status. Consumers must call `verify_finalization`; a missing, malformed,
or mismatching seal yields `INCONCLUSIVE`. The seal is an integrity checksum,
not an authenticated signature or provenance proof. Finalization errors return
an in-memory incomplete result and preserve any exclusive partial file; they
never produce a valid COMPLETE seal.

The global persisted-byte maximum is unchanged. The default output reserve is
64 KiB for the final manifest plus 4 KiB for the seal, inaccessible through the
adapter budget facade. Each output also has a fixed per-file limit. The
orchestrator checks room for the reserve and a running-output floor before
creating an acquisition directory.

The budget manager claims its first calling thread and rejects other threads.
It is a correctness/accounting mechanism, not a security boundary against
Python code in-process. Adapters and source implementations must be treated as
trusted, reviewed code. The facade does not expose output/finalization methods,
but Python name-mangling is not a privilege boundary.

## Authorization contract

`authorization.py` models separate signed `APPROVE` decisions for (a) exact
candidate digest, target, read set, module allowlist, and budget ceiling, and
(b) execution bound to that read-set approval. Both approvals have distinct
one-use nonces, issue/expiry times, distinct decision-maker identifiers,
distinct key IDs and key material, and HMAC-SHA256 signatures. Verification is fail-closed and happens before output
creation or adapter dispatch.

This is a testable contract only. The verifier does not issue approvals or
create keys. Trust keys must come from an external controlled trust source;
the implementation's replay ledger is in-memory and is not adequate for an
operational multi-process/restart environment. No operational authorization
credential or approval was created in Session 57. The current verifier uses a
shared-secret HMAC model; human identity, key custody, revocation, durable
nonce consumption, separation of duties, and approval UI/service remain
unqualified operational dependencies.

Nonce consumption occurs before the output directory is created. A startup
failure (including ENOSPC while writing `manifest.running.json`) returns
`INCONCLUSIVE` without entering an adapter; the exclusive partial running file
is retained. Because the current nonce ledger already consumed both nonces,
retry requires fresh approvals. This ledger behavior is process-local and does
not provide restart-safe replay protection.

## Adapter contracts

### identity

Accepts exactly six fixture-provided fields: lowercase 32-hex system ID,
bounded kernel and distribution strings, enumerated virtualization value,
canonical UUID boot ID, and bounded acquisition environment ID. It does not
open `/etc/os-release`, `/etc/machine-id`, `/proc`, or any real path.

### systemd-state

Accepts only the exact service IDs listed in the historical Alpha inventory
(the code also validates namespace syntax) and the approved unit property
fields. At most 32 units are enumerated; the current fixed inventory contains
15 IDs drawn from the documented historical unit inventory. Duplicate or foreign IDs and unknown fields fail parsing; missing
properties yield `INCONCLUSIVE`. A validated `ControlGroup` is persisted only
as a presence boolean; no cgroup path is emitted. It does not read journals and
does not infer process absence from `MainPID`.

### process-cgroups

Accepts only PID/source-generation references, PID/PPID/UID/state/source
snapshots, and cgroup paths. At most 4096 PID references and 128 Alpha process
records are handled. `argv`, `cmdline`, and process names are neither accepted
nor emitted. Generation/source IDs detect a PID reuse mismatch where the
abstract source supplies them; only their SHA-256 digest is persisted. Cgroup
membership is checked before requesting PPID/UID/state metadata, so non-Alpha
processes do not receive those additional reads. Reports retain the exact Alpha
unit ID and omit full cgroup paths and descendant names. Process enumeration,
membership, and process snapshots carry a shared source snapshot ID; only its
SHA-256 is persisted, and missing or mismatched IDs make reconciliation
inconclusive. A future backend must provide a stable, generation-aware snapshot
contract. Sequential live `/proc` reads alone cannot prove simultaneous absence.
Missing/disappeared/reused PIDs, unreadable or invalid cgroups, incomplete
enumeration, and truncation are non-positive.
Absence is asserted only when the source explicitly reports complete inventory,
every membership is known, and no Alpha process is present. This is a
synthetic reconciliation contract; no `/proc` or cgroup backend exists.

## Accounting and filesystem controls

The budget tracks access attempts and terminal outcomes, enumerated/read file
counts, PID and unit enumeration, bytes read and persisted, records, process
and unit counts, interruption count, per-category limit events, and duration.
On successful finalization the manifest labels access/operation counters as
pre-finalization; finalization file and byte totals are projected, while the
final manifest/seal syscall counters are excluded to avoid self-reference. A
failed finalization snapshot includes operations observed up to that failure.
Filesystem reads use open directory descriptors, `O_NOFOLLOW`, `O_CLOEXEC`,
`O_NONBLOCK`, regular-file checks, single-link checks, bounded reads/hashes,
and pre/post `fstat` identity and metadata comparison. Outputs use exclusive
descriptor-relative creation, bounded writes, short-write loops, per-file
SHA-256, `fsync`, and directory `fsync`; failures preserve partial files and
are not retried under the same output name.

These protections do not stop a concurrent process with the same UID from
changing writable parent directories or manipulating other same-UID state.
No `openat2` helper is included. In-memory structured sources are trusted to
honor bounds before materializing data; size accounting after a source method
returns cannot retroactively bound work or bytes already consumed by an
unreviewed source implementation. Physical source bytes are accounted by the
source through `AdapterBudget.record_source_bytes`; adapter-side JSON encoding
is only a size/schema check and is not charged a second time. A concrete source
must request a budget allowance before each read and charge bytes actually
consumed, including bytes consumed before an error. Synthetic fixtures charge
the canonical representation of returned records. The orchestrator currently accepts any
in-process `DiagnosticSource` object and does not prove that it is bound to the
approved target. Before any real backend exists, each source operation must be
independently implemented with pre-read allowances and descriptor-stable
access, then bound to the target/candidate approval. The candidate digest is
supplied by the caller; the core does not measure the code being run. A future
operational gate must hash the executed candidate externally and compare it
with the signed approval.

## Synthetic qualification

Command:

```text
PYTHONDONTWRITEBYTECODE=1 python3 -B -m unittest -v tests.alpha_m1_collector_v2_test
```

Session 57 run: **29 tests PASS**, zero FAIL/ERROR/SKIP/TIMEOUT. Coverage
includes descriptor/path/symlink/hard-link/FIFO checks, bounded reads and
hashes, mutation during read, output collision and partial writes, deadline
accounting, reserved finalization budget, single-thread enforcement, auth
expiry/tampering/replay/target binding, adapter allowlists and schema
redaction, duplicate/foreign/missing systemd data, 33 units, 4097 PID refs,
129 Alpha processes, PID reuse, missing/denied cgroups, interrupted
enumeration, serialization fallback, final-manifest seal verification, and
unselected module non-dispatch, interruption stopping later dispatch, short
writes, ENOSPC after partial progress, and filesystem enforcement of the
reserved output ceiling.

All fixtures are synthetic; these tests do not establish behavior against
systemd, `/proc`, cgroups, the guest VM, or administrative evidence. The three
review outcomes are summarized below for human review. Every acquisition
without a valid final seal is `INCONCLUSIVE`; no separate durable failure
sidecar is written if finalization fails, so partial files never establish
completion.

## Independent review results

- **Core review:** the synthetic contracts and status propagation passed after
  fixes for adapter-interruption dispatch, source-byte double accounting,
  reserved output writes, strict unit/cgroup validation, and inventory epoch
  matching. The reviewer confirmed the 29-test suite passes. Measuring the
  running candidate and binding the real source/target remain operational gates.
- **Security review:** confirmed cgroup-first filtering, digest-only source and
  snapshot identifiers, redacted cgroup paths, exact unit allowlist, separate
  approver identities and key material, and rejection of mixed inventory
  epochs. Caller-supplied candidate/target claims, trusted in-process sources,
  HMAC key custody, and durable replay protection remain gates before any real
  backend.
- **Operations review:** confirmed startup write failures return
  `INCONCLUSIVE` before adapter dispatch, finalization without a valid seal is
  inconclusive, and partial outputs are retained. Fresh approvals are needed
  after a failed startup because this candidate consumes nonces before output
  creation. No real acquisition was attempted.

The reviews apply only to this fixture-only candidate and do not qualify a live
backend or close Alpha M1 gates.

## Qualification impact and remaining gates

No Session 49 component or promoted digest is updated or promoted here. The
Session 56/57 candidate has its own digests and requires independent review,
synthetic fault testing, and a new explicitly reviewed candidate promotion
before any later integration. Adoption would require a new read-set decision
for exact source operations, modules, budgets, output destination and target,
plus a separate execution authorization. Neither is present here.

The approved historical corpus and its baseline are untouched. Historical
campaign results (including BLOCKED and NOT RUN cases), guest/host assertions,
and the missing Session 47 comparison remain unchanged. This candidate cannot
close any historical gate. A real backend, operational authorization trust
and replay service, independent review, approved read set, guest preflight,
acquisition, and human evidence review remain open. **Alpha M1 remains
BLOCKED.**
