# Alpha M1 Session 56 — Collector v2 core candidate

## Status and boundary

This document describes an isolated, unpromoted Python core candidate in
`qualification/alpha_m1_collector_v2/`. It does not replace or modify the
historical collector, runner, bootstrap, CTest helper, or the promoted Session
49 pins. All eight module adapters are placeholders: a selected module reports
`INCONCLUSIVE` and does not inspect the host, guest, journal, process table,
administrative paths, or evidence corpus. There is no command-line entry point.
The core is not approved for diagnostic acquisition. Alpha M1 remains BLOCKED.

## Contract

Each manifest uses schema version 1 and contains a candidate/acquisition ID,
explicit authorization reference and module selection, fixed scope descriptions,
hard budget limits and usage, bytes read/persisted, record count, elapsed
monotonic duration, per-module states, errors/anomalies, and SHA-256/size records
for outputs. `manifest.final.json` cannot contain its own digest; the adjacent
exclusive `manifest.final.sha256.json` seals its exact bytes. This seal is an
integrity checksum, not a signature or proof of authorization. A `running`
manifest is left as incomplete evidence if the process stops before finalization.
Adapter errors/anomalies are persisted only as bounded allowlisted codes;
unknown free text is replaced with a redaction marker. Summaries accept only
the fixed synthetic-placeholder fields in this phase. Future modules need
explicit per-module structured output schemas before any new values can persist.

States are `COMPLETE`, `TRUNCATED`, `ACCESS_DENIED`, `ABSENT`, `PARSE_ERROR`,
`TIMEOUT`, `INCONCLUSIVE`, and `NOT_REQUESTED`. The current adapters only emit
`INCONCLUSIVE`; unselected modules are always `NOT_REQUESTED`. Aggregation
returns `COMPLETE` only if every selected result is `COMPLETE`, which the fixed
placeholder dispatch cannot produce. Truncated or interrupted files remain
exclusive partial outputs and are described as incomplete.
The authorization reference and selected module list are caller-supplied
metadata only; they do not authenticate a human approval. Future adapters must
not treat these fields as authorization proof.

## Fixed budgets

| Budget | Candidate default and hard maximum |
|---|---:|
| Total bytes read | 576 MiB |
| Total bytes persisted | 4 MiB |
| Files opened | 256 |
| Records | 8,192 |
| Processes | 4,096 |
| Systemd units | 32 |
| Duration | 180 seconds |
| One input file | 128 MiB |
| One module/source | 256 MiB |
| One output file | 64 KiB |

Callers may lower limits but cannot raise any hard maximum through configuration.
The 576 MiB aggregate read cap intentionally exceeds the 256 MiB source cap to
permit a bounded future multi-source acquisition; module identifiers are fixed
and validated so arbitrary source labels cannot split one source budget. Values
are provisional and require security/operations review before any future
adapter is enabled. Every successful `read` and `write` is charged at the
descriptor operation, including if the deadline expires immediately afterward.
Successfully opened and validated input files, plus every output creation
attempt, count toward the file limit. Failed pathname opens and enumeration
entries do not currently count; a future enumerator must apply its own fixed
entry bound before use. An over-budget input is rejected before reading based
on its opened descriptor's `fstat` size. The orchestrator rejects limits too
small for the minimum running/final manifest, seal, and selected-module output
set observed with current placeholders; this is a floor, not protected reserve.

## Filesystem guarantees and limits

Directory walking and leaf opens use descriptor-relative operations with
`O_DIRECTORY`, `O_NOFOLLOW`, `O_CLOEXEC`, `O_NONBLOCK` for inputs, and exclusive
creation for outputs. Input objects must be regular files with exactly one
link, fit the declared cap, and keep device/inode/type/link-count/size/mtime/
ctime stable over the read. Reads and hashes use the same descriptor. Output
files use `O_EXCL|O_NOFOLLOW`, bounded partial-write loops, SHA-256 over bytes
actually written, file `fsync`, then directory `fsync`. They are mode `0600`
inside a newly created `0700` directory; failed output is not silently retried
or overwritten.
Raw descriptor attributes are name-mangled and are not exposed as part of the
module API, but Python cannot enforce a security boundary against a malicious
adapter in the same process. Future adapters remain trusted code and require
static review; a hostile adapter needs process isolation or a native capability
boundary. Output directory ownership/mode and inode identity are checked after
the descriptor open.
Budget accounting and read allowance checks are single-threaded. The
orchestrator dispatches selected placeholders serially; parallel adapters must
not be enabled until read reservations and counters are made atomic.

The final manifest projects the total successful persisted bytes and output
file count to include its own bytes and the detached seal. Its elapsed duration
is measured through module completion and intentionally excludes final
serialization/fsync time; a crash during finalization can leave an incomplete
running manifest, a final manifest without a seal, or a partial seal. Those
states are not complete evidence.
The orchestrator's output-budget floor is not a reservation against future
module output: any real adapter must reserve adequate finalization capacity
before it is enabled. The current placeholder-only run has no adapter-generated
output pressure.

These checks reduce pathname substitution and accidental mutation races; they
do not defeat a malicious concurrent process with the same UID that can alter
the output parent or repeatedly mutate open files. A descriptor pins an inode,
but same-UID concurrency can still cause denial of service or mutation during
read; before/after metadata checks cannot prove that no transient mutation
occurred and was restored under all filesystems. There is no `openat2` helper in
this candidate. Bash would not provide comparable descriptor and partial-write
control. If stronger adversarial same-UID guarantees become required, use a
small audited native helper or a separately isolated service identity.

## Read-set ceiling for later design

The module identifiers and scope labels are contracts only, not approvals or
implemented accesses:

| Module | Candidate scope label | State in this candidate |
|---|---|---|
| `identity` | Fixed guest identity fields | Placeholder, no reads |
| `systemd-state` | Alpha unit names and allowlisted state properties | Placeholder, no reads |
| `process-cgroups` | PID inventory, cgroup and Alpha PID status fields; never argv/cmdline | Placeholder, no reads |
| `paths-metadata` | Fixed path metadata only; D6 payload excluded | Placeholder, no reads |
| `build-integrity` | Fixed manifests, generated inventory, artifact hashes | Placeholder, no reads |
| `journal` (optional) | Bounded selected Alpha records, only after separate approval | Placeholder, no reads |
| `receipt-hash` (optional) | One exact bounded receipt hashed without serialization | Placeholder, no reads |
| `backup-hash` (optional) | Allowlisted bounded backup files hashed without serialization | Placeholder, no reads |

The minimum profile names the first five modules and excludes the three optional
modules. Even this profile has no acquisition capability at present. No payload
path for D6 is present in an adapter, and this implementation does not read any
path. Future review must separately approve each optional module and document
its precise source list, byte and item limits, data sensitivity, and retention.

## Test boundary and next qualification

`tests/alpha_m1_collector_v2_test.py` uses only `TemporaryDirectory` fixtures.
It exercises descriptor/no-follow behavior, hard links and special files,
bounded reads, mutation detection, descriptor anchoring across rename, output
exclusivity, partial writes, authorization checks, incomplete-status mapping,
placeholder/NOT_REQUESTED states, interrupted writes, and output serialization.
Test execution does not invoke
the historical collector or any system diagnostic command.

Passing synthetic tests qualifies only this core's tested invariants. It does
not close historical campaign BLOCKED/NOT RUN results, establish guest identity,
authorize a read set, approve the three optional modules, or authorize actual
collection. Preserve this candidate separately; future revisions need a new
candidate digest set and explicit review. The Session 49 baseline is unchanged.
