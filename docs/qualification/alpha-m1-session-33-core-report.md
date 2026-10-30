# Session 33 — Core review of the systemd canary

## Scope and observed state

Reviewed canary, runner, gateway support, installer receipt validator, sudoers
draft, and targeted regressions. Corrected terminal cgroup confirmation in the
canary, runner and support helper. No live VM access occurred; VM observations
remain externally supplied.

The working tree was already dirty at `abd6bac4b368427b76f6c98522afde928b418939`
on `main`. The qualification sources, tests, build directories, reports and
evidence were already untracked. Existing changes and evidence were preserved.
This review changed only the canary, runner, gateway support, focused Python
regressions, and this report.

## Container hashes

| Artifact | SHA-256 |
|---|---|
| Canary candidate | `caeb095d1e9a5f3bb631a415f49f0f039ab36874352cba43a86c0a6207440893` |
| Runner candidate | `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` |
| Gateway support candidate | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` |
| Installer candidate, unchanged | `55f13e8d1d91e149b6f24a8257fa48339f2b48e2a1b619f5ae0a5b1053d463a8` |

The VM evidence supplied to this session reports installed runner SHA-256
`5d6ee35070954d97034de7f0323204995fe0ca86ed12fad774a3acb9ada021d5` and
candidate runner SHA-256
`2290b78c55c72489c0e7dc3bb1c1fedbc573e6bc605e690b6afd3a8ab326b24c` (the
pre-fix candidate). These VM observations were not independently verified in
the container.

The runner's embedded support trust anchor was changed as an explicit
candidate proposal: `SUPPORT_SHA256` from
`86be38660154161b6f71063567241fed3fbe1ef270d04a73b7c00a69126a9d28` to
`92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514`. This
change has local regression coverage and is not approval to promote it. The
installer pins the old runner (`2290b78c55c72489c0e7dc3bb1c1fedbc573e6bc605e690b6afd3a8ab326b24c`),
support (`86be38660154161b6f71063567241fed3fbe1ef270d04a73b7c00a69126a9d28`)
and canary (`0c2567320d3f5c461d0c9a4a19beaf62b83615c511c28e699810bbdfa49f3107`).

The pending installer pin proposal is exactly:

```diff
-RUNNER_SHA256=2290b78c55c72489c0e7dc3bb1c1fedbc573e6bc605e690b6afd3a8ab326b24c
+RUNNER_SHA256=c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def
-SUPPORT_SHA256=86be38660154161b6f71063567241fed3fbe1ef270d04a73b7c00a69126a9d28
+SUPPORT_SHA256=92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514
-CANARY_SHA256=0c2567320d3f5c461d0c9a4a19beaf62b83615c511c28e699810bbdfa49f3107
+CANARY_SHA256=caeb095d1e9a5f3bb631a415f49f0f039ab36874352cba43a86c0a6207440893
```

This proposal was not applied; the above is an explicit candidate diff only.

## Compatibility findings

- The canary is a standalone root-invoked systemd probe, imports no gateway
  support, and executes no Athena binary. Its receipt schema remains compatible
  with installer validation: schema 1, pass boolean, canary/runner hashes, boot
  ID, systemd version and nanosecond timestamp. The installer enforces current
  boot and a 24-hour freshness window.
- Canary timeout or interruption requests stop, then bounded SIGKILL escalation.
  An uncertain state preserves markers and does not publish a receipt. Existing
  receipt evidence is hard-linked to a unique archive before atomic replacement.
- The canary now requires exact unit identity, terminal state, MainPID 0, and
  either an observed `populated 0` or a narrowly justified cgroup disappearance.
  A vanished cgroup is accepted only after this invocation's `systemd-run
  --wait` returned zero and systemd still reports the exact unit ID, terminal
  state, MainPID 0 and `Result=success`. Empty/vanished cgroups on failed,
  timed-out, interrupted or ambiguous execution remain unknown and prohibit
  cleanup and receipt publication.
- The runner applies the same distinction. Its systemctl query now includes
  `Id`; a mismatched unit identity is rejected. Empty or missing cgroup paths
  and missing `cgroup.events` are represented as unknown. `gateway_support`
  permits that state only when the caller supplies a positively successful
  systemd-run completion; timeout/recovery waits do not.
- D6 evidence was not inspected or altered. The canary's ordinary cleanup
  concerns only its private temporary markers and is gated by the termination
  decision described above. This review does not authorize deletion or movement
  of D6 evidence.
- The canary remains absent from sudoers. No privilege or gateway feature was
  added.

The pre-fix defect was reproducible in logic and tests: an empty ControlGroup or
missing cgroup file could count as termination. The fix distinguishes cgroup
removal after a successful, identity-bound `systemd-run --wait` from removal
observed after timeout or other uncertain outcomes. Live systemd behavior still
requires target-VM confirmation.

## Verification

- **PASS:** 24 canary/admin tests after correction.
- **PASS:** 15 gateway hardening tests after correction.
- **PASS:** `py_compile` for canary, runner and gateway support.
- **FAIL:** 0 in the final rerun.
- **SKIP:** 0.
- **TIMEOUT:** 0.
- **NOT RUN:** live systemd/cgroup canary, privileged tests, protected-path
  receipt publication and installer execution.

The tests use local fixtures/mocks and do not prove runtime cgroup behavior on
the VM. The installer hash static test now explicitly reports runner, support
and canary pin differences as pending promotion; it does not change them.

## Core disposition

The cgroup logic has focused local regression coverage. Security review must
approve the candidate logic, the proposed runner trust-anchor change and the
exact installer pin updates before the bundle can proceed to administrative
approval. No installation, installer pin update, privileged invocation or VM
mutation was performed by Core.
