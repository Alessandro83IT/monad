# Alpha M1 Session 63 — synthetic descriptor lifecycle hardening

**Verdict: SESSION 63 PASS WITH CONDITIONS. Alpha M1 remains BLOCKED.** Work
was limited to the test-only Session 62 handoff. No collector, historical
runner, diagnostic backend, service, journal, real process/cgroup, D6 payload,
administrative path, or installed system was accessed. No privileged test or
operation was run, and no digest was promoted.

## Milestone completed

The native test handoff previously kept the parent process's copies of all
source memfds open until the child runtime had exited. Immediately after
`fork()`, the parent now clears both RAII descriptor collections. The child
retains its forked copies, which are the only copies it needs to import the
verified source bytes. This shortens parent-side descriptor lifetime; it does
not establish a hostile same-UID boundary or any property of production
execution.

A test-only fault injection makes the child wait briefly after startup, inspect
the native parent's `/proc/<pid>/fd` entries, and fail if it finds a source
memfd. Its regression case passes through the complete synthetic handoff and
nonce lifecycle. The production target does not compile this test path.

The runtime error cleanup previously used blocking `waitpid()` after sending
`SIGKILL` on several exceptional paths. Those paths now use `WNOHANG` with a
one-second deadline and report `runtime_cleanup_incomplete=true` when the
direct child is still not reapable. The setpgid failure cleanup follows the
same bound. A synthetic poll-error injection covers this path, checks that the
parent returns within three seconds, and confirms that the reserved nonce
remains unavailable. This test does not reproduce an uninterruptible kernel
task; it covers the fault-injected poll error and bounded reap implementation.

## Files changed

- `qualification/alpha_m1_collector_v2/native/verified_bootstrap.cpp` — close
  parent memfd copies after fork; bound cleanup waits; add test-only parent
  descriptor inspection and poll fault injection.
- `qualification/alpha_m1_collector_v2/tests/alpha_m1_native_gate_test.py` —
  regressions for parent descriptor closure and bounded poll-error cleanup.
- `qualification/alpha_m1_collector_v2/native/README.md` — document the
  lifecycle behavior and the limits of its test.
- `docs/qualification/alpha-m1-session-63-candidate.md` — this report.
- `qualification/alpha_m1_collector_v2/tests/__pycache__/alpha_m1_native_gate_test.cpython-313.pyc` — generated when Python ran the requested suite; retained in the worktree.

These paths were already untracked in the initial worktree. Existing worktree
changes and artifacts were preserved.

## Verification

| Check | Result | Detail |
|---|---|---|
| Synthetic native gate and handoff suite | **PASS — 54 tests** | `python3 -m unittest -v qualification.alpha_m1_collector_v2.tests.alpha_m1_native_gate_test` |
| New parent descriptor lifecycle regression | **PASS** | Child observed no source memfd held by its parent after fork |
| New poll-error cleanup regression | **PASS** | Return under three seconds; child reaped; nonce remained RESERVED and retry was REPLAY |
| Production CMake Release configure/build | **PASS** | Fresh build at `/tmp/alpha-m1-session63-build`; GCC 14.2.0, OpenSSL 3.5.7 |
| CTest | **NOT RUN** | No CTest cases were run in this work item |
| Privileged/operational tests | **SKIP / PROHIBITED BY MANDATE** | No sudo, install, collector, acquisition, or handoff operation |
| Test failures/timeouts | **FAIL: 0; TIMEOUT: 0** | The unittest run completed successfully |

The CMake production executable's SHA-256 is unchanged from the Session 62
reported value. That is consistent with this source path being guarded by test
compile definitions and the production CMake target remaining verify-only.

## Fault corrected and residual conditions

The corrected issues were retention of duplicate parent-side memfd descriptors
throughout runtime execution and unbounded blocking waits on several cleanup
paths. The descriptor regression demonstrates the tested `/proc` observation
after the native parent has had time to perform its synchronous close. The
cleanup regression covers a fault-injected poll failure where `SIGKILL` is
followed by a successful reap. Neither test proves behavior under every
scheduler/kernel failure or termination of an uninterruptible task.

Unresolved conditions remain those recorded in Sessions 58–62: the bootstrap,
interpreter, standard library, dynamic loader, native dependencies, toolchain,
kernel, and pre-main environment are not authenticated by the candidate
manifest; test trust material is not operational trust; the local nonce ledger
can be rolled back or replaced by a same-UID actor; power-loss durability is
unqualified; process-group cleanup is not a general OS sandbox; no production
handoff exists. No claim is made that Python is a security boundary.

## Baseline and candidate artifact hashes

Initial repository state was branch `main`, `HEAD` `abd6bac4b368427b76f6c98522afde928b418939`, with pre-existing tracked modifications, untracked evidence, and build directories. Those were preserved. No historical baseline files or promoted digests were intentionally modified. The historical corpus and its cited aggregate digest were **not re-read or revalidated** in this session. The candidate identity manifest was not regenerated; the change is in native test-only code and is outside that Python component manifest.

| Artifact | SHA-256 |
|---|---|
| `qualification/alpha_m1_collector_v2/native/verified_bootstrap.cpp` | `f593cf770fa20067b1f57bb2b5e6593baded6b8fc737079b69149a2d014d4d5f` |
| `qualification/alpha_m1_collector_v2/tests/alpha_m1_native_gate_test.py` | `4aaa31874fe1c77008943b18db1f02adf0604e2a0147417637f35e11bc8e3879` |
| `qualification/alpha_m1_collector_v2/native/README.md` | `00b2046f5773aee31dbbcf3b00d87788a22cb3da9a6c59933712aaccfd601473` |
| `/tmp/alpha-m1-session63-build/alpha_m1_native_verified_bootstrap` | `11050b06577a97135ac0015086de4b3cf61a6edd9509f2b539c5800b380eb265` |
| `qualification/alpha_m1_collector_v2/tests/__pycache__/alpha_m1_native_gate_test.cpython-313.pyc` | `56358c992e3a36f3928cd448c58c399adf11ec580f7a47924265424adf0cf383` |

No historical-baseline artifact hash is newly asserted. The report's own hash
is recorded separately in the Session 63 completion message to avoid a
self-referential digest.

## Next work proposal and human decisions

Continue with synthetic-only fault injection for output-channel and
start-channel failures, and review whether all cleanup error paths report
child state consistently. Separately, a human-reviewed design decision is needed
before any work changes trust-root provisioning, bootstrap/runtime
authentication, production authorization, the nonce rollback model, or
administrative qualification. No such decision or operational authorization
was made here.

**Alpha M1 status: BLOCKED.**
