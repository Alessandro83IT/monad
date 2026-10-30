# Alpha M1 Session 59 — native bootstrap candidate

**Verdict: SESSION 59 PASS WITH CONDITIONS. Alpha M1 remains BLOCKED.** This
deliverable is an unpromoted native **verification-only** prototype. It does
not run Python or a diagnostic collector and does not consume an operational
nonce.

## Architecture and trust boundary

`qualification/alpha_m1_collector_v2/native/verified_bootstrap.cpp` is a C++20
verifier using OpenSSL EVP/HMAC for SHA-256. It accepts only
`--verify-only <absolute-repository-root>`. Unknown flags, arbitrary scripts,
roles, or any launch mode fail closed. It contains no shell/process creation
calls and has no Python execution path.

For a synthetic fixture, native code uses a fixed HMAC-SHA256 test anchor
compiled in the source. It authenticates the exact bytes of
`candidate_identity.json` before parsing them. It rejects duplicate JSON keys,
unknown/missing schema fields, wrong candidate/contract, entry point/read set
or budgets that differ from code-fixed policy, and a non-exact component list.
The manifest and HMAC sidecar are fixture inputs; no key or sidecar was added
to the repository. Since the symmetric test anchor is public in source and in
the compiled binary, it is not an operational root of trust.

The candidate repository root and each path component are opened without
following symlinks. The package directory inventory is checked before and
after verification. Each manifest and component file is opened relative to
the package directory with `O_NOFOLLOW`; the verifier checks regular-file
type, effective-UID ownership, one link, size limits, byte count and stable
`fstat` identity. Component SHA-256 values and the Python candidate's framed
aggregate format are independently recomputed in native code. The profile is
fixed at 14 Python files, 1 MiB per component, 8 MiB aggregate component bytes
(the remaining allowance is applied before each component read), with the
Session 58 maximum modules and exact hard budget values. Manifest and HMAC
sidecar have separate 128 KiB and 128 byte limits.

## Bytes checked and bytes executed

The verifier reads the manifest, signature sidecar, and 14 Python component
files into bounded memory and checks their digests. It discards those bytes on
exit. **No bytes are executed.** Therefore this prototype makes no claim that
bytes hashed from a pathname are the bytes later imported or run; there is no
later import, child process, or launch transition to race against. Concurrent
mutation during an open file read is fault-injected and rejected; directory
membership and metadata are rechecked. A same-UID writer can still alter the
tree immediately after the final check, which matters for future use but does
not convert this verify-only result into an execution claim.

## Session 58 integration and intentionally absent behavior

Native validation checks the Session 58 candidate identity, maximum read set,
and hard budgets. It does **not** validate Python trust-store roles, Candidate
Approver signature, target attestation, separate Read-set and Execution
approvals, local nonce ledger, or target identity. It does not accept those
as caller-supplied assertions. There is no authorization handoff to Python
and no nonce consumption because no run can be authorized here. Flags such as
`--execute`, `--run-synthetic`, and `--role=...` are rejected.

This verify-only boundary remains until the C++ side has one canonical,
independently reviewed contract for all four roles and the durable nonce
transaction, without relying on the Python caller. Session 58's
`verified_launcher.py` imports and executes before its own measurement, so
calling it from this bootstrap would not close the trust chain.

## Runtime and external dependencies

The CMake candidate requires C++20, CMake 3.20+, and OpenSSL 3 Crypto. Build
instructions and the fixture protocol are in
[`qualification/alpha_m1_collector_v2/native/README.md`](../../qualification/alpha_m1_collector_v2/native/README.md).
The binary is dynamically linked. Its C++ code cannot authenticate the
interpreter, OpenSSL, libc, dynamic loader, compiler, kernel, or bootstrap
binary itself. Loader variables such as `LD_PRELOAD` take effect before
`main`; the verifier does not claim to clear them. A compiled-in root is useful
only after an external party measures/authenticates the bootstrap and its
launch environment. The `native/` source and build files themselves are not in
the Python identity manifest.

A later synthetic runner could use a fixed interpreter path and controlled
environment (`-I` implies isolated mode, ignores Python environment variables
and disables user site; `-S` suppresses `site` initialization; `-E`/`-s` are
already subsumed by `-I`). These flags do not verify CPython, standard-library
modules, extension modules or native dependencies. Any candidate module import
must resolve exclusively to already verified bytes; this candidate does not
implement that importer or a memfd transfer.

## Tests and review

The native test suite builds a verifier with strict warnings in a temporary
directory and builds a temporary `LD_PRELOAD` fault shim. Tests use copied
candidate sources and synthetic HMAC fixtures only. They cover valid manifest
verification; manifest/HMAC alteration and missing HMAC; wrong component
digest, missing/extra files, symlinked file/directory, hard link, FIFO,
oversize component, short reads, mutation during the read, JSON duplicate/depth
and node limits, directory-entry cap, broken output pipe, process interruption,
contaminated Python variables and a marker-writing import payload, arbitrary
execution options, and concurrent verify-only invocations. The marker is not
created; successful output states `python_started=false` and
`nonce_consumed=false`.

The final targeted run passed 15 native tests, 17 Session 58 trust-chain tests,
and 29 Session 56/57 core tests (61 total). CMake configure/build succeeded with
`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror`. No CTest,
real collector, service, journal, process, cgroup, guest, or D6 payload was
accessed. The preload shim is only test fault injection; it is not a trust
boundary. This local CMake build observed GCC 14.2.0 and OpenSSL 3.5.7; these
versions were not pinned or independently authenticated.

| Area | Result | Note |
|---|---|---|
| Native manifest/component verification | PASS | Synthetic authenticated fixture only |
| Path, size, link, mutation and parser fault injection | PASS | Symlink/hardlink/FIFO, short read, mid-read mutation, aggregate cap, JSON caps |
| Runtime environment/import payload | PASS | Native verifier did not start Python; marker stayed absent |
| Roles, signed target, expiry, nonce/replay in native code | NOT RUN | No native authorization parser or launch path exists |
| Crash after native nonce consumption | NOT APPLICABLE | Native verifier never consumes a nonce |
| Verified bytes equal executed Python bytes | NOT DEMONSTRATED | No candidate bytes are executed |
| Power-loss durability / privileged VM behavior | NOT RUN | Outside this verify-only synthetic qualification |

Independent Core, Security and Operations reviews completed. Core found no
schema/aggregate/CLI defect. Security and Operations found unbounded directory
enumeration; Security also found recursive JSON parsing without depth/node
caps. Operations found success could be returned when stdout failed. The
implementation now caps package entries/names and parser depth/nodes, checks
stdout flush status, and applies remaining aggregate allowance before each
component read; adversarial tests cover these cases. Operations also requested
a unique build directory, now implemented using `mktemp`; no later review found
further defects in the scoped code. Reviews
confirmed remaining trust conditions: public synthetic HMAC key, dynamic
loader/`LD_PRELOAD`, and no native binary self-authentication. The native CMake
target needs an independently pinned toolchain/dependencies and external
binary digest before any future trust claim. Those limitations keep the only
mode verification-only.

## Baseline and qualification status

No promoted component, historical corpus, service, snapshot, or administrative
path was modified. The latest historical corpus check recorded in Session 58
reported 149 files and aggregate SHA-256
`f229f1b367f31053a283f564b3d2fa3edd713bfa1ede5134c899466f1bb8f67f`, PASS
local only. Session 59 did not re-read that corpus. Historical controls remain
109 PASS, 34 BLOCKED and 7 NOT RUN. No digest is promoted. Alpha M1 remains
BLOCKED.

## Candidate artifact digests

SHA-256 values from the final reviewed state:

| Artifact | SHA-256 |
|---|---|
| `native/verified_bootstrap.cpp` | `60e94a079391f6f378c880e419a51a9c5f4ef0d7f9f50f958ba3adc4660c8de8` |
| `native/CMakeLists.txt` | `f911f4f6435c223acf97ff82d6a8ecd4bb9c15048edaddccde4e3bbadb944316` |
| `native/README.md` | `7252f7e9a0b66e77dec58d674fedaeca994c776814284b3dcc3ee8c89e289d56` |
| `tests/alpha_m1_native_bootstrap_test.py` | `87cf8fc8e3b8a31547c275503e33275857c4f822bb24be302a4f8d2381c15ef6` |
| Local Debug executable in `/tmp/alpha-m1-session59-native-build/` | `ae52a6a2930f4fb184f87eeb02146f5f75525e487e93d8ead9444923b61c7997` |

The executable digest identifies this local build only. Its compiler, OpenSSL,
standard libraries and build environment were not independently pinned or
authenticated; the value is not a promoted pin or operational trust anchor.

## Next session

Session 60 should design a native authorization wire contract for the four
Session 58 roles, target attestation, code manifest, budgets and nonce ledger.
It should define an externally authenticated trust root and revocation epoch,
then review a sealed-FD transfer protocol for the fixed synthetic entry point.
Only after Core/Security/Operations review and adversarial tests should any
synthetic Python runtime handoff be considered. Operational launch remains a
separate future approval and is not implied by that work.
