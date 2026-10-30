# Alpha M1 Session 33 — integrated promotion readiness

**Decision: BLOCKED.** The cgroup termination defect found during review has been corrected in the local canary/runner/support candidates and independently re-reviewed. The installer still performs a state-changing automatic rollback after some post-marker install failures. That crosses the separate rollback approval boundary and must be corrected and reviewed before administrative promotion. The proposed hash updates remain unapplied. No component was installed, no privileged test or live canary was run, and the VM state was not changed.

## Agent deliverables

- [Core compatibility review](alpha-m1-session-33-core-report.md): cgroup handling, compatibility, source hashes, tests, and proposed installer pin diff.
- [Verification report](alpha-m1-session-33-verification-report.md): supplied manifest provenance, hashes, protected/isolated inventory, and VM/container evidence separation.
- [Independent Security review](alpha-m1-session-33-security-report.md): the cgroup change is statically closed for the reviewed hashes; automatic rollback remains a blocker. Security did not approve promotion.
- [Operations report](alpha-m1-session-33-operations-report.md) and [administrative procedure](alpha-m1-session-33-admin-procedure.md): repeatable staged procedure with explicit approval gates and D6 preservation.

All four agents returned substantive reports. The coordinator ran the final non-privileged regression sets and generated a new post-fix reconciliation manifest.

## Candidate corrections and local hashes

Core changed the canary, runner and gateway support so a missing cgroup is indeterminate by default. Cgroup disappearance is accepted only after this invocation's `systemd-run --wait` succeeds and systemd still reports the exact unit ID, terminal state, `MainPID=0`, and `Result=success`. Timeout, interruption, mismatched identity, and ambiguous state do not permit cleanup or receipt publication. The runner's embedded support digest was changed as a proposed candidate trust-anchor update.

The current container hashes are:

| Candidate file | SHA-256 |
|---|---|
| `qualification/athena-alpha-m1-systemd-canary.py` | `caeb095d1e9a5f3bb631a415f49f0f039ab36874352cba43a86c0a6207440893` |
| `qualification/athena-alpha-m1-runner.py` | `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` |
| `qualification/gateway_support.py` | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` |
| `qualification/install-athena-alpha-m1-admin.sh` (unchanged) | `55f13e8d1d91e149b6f24a8257fa48339f2b48e2a1b619f5ae0a5b1053d463a8` |
| `qualification/athena-alpha-m1-sudoers` (unchanged) | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` |

These are container observations. The session-supplied VM candidate runner hash `2290b78c55c72489c0e7dc3bb1c1fedbc573e6bc605e690b6afd3a8ab326b24c` is the pre-fix candidate; VM hashes were not independently collected.

## Pin proposal — not applied

The exact proposed three-line installer pin diff is:

```diff
-RUNNER_SHA256=2290b78c55c72489c0e7dc3bb1c1fedbc573e6bc605e690b6afd3a8ab326b24c
+RUNNER_SHA256=c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def
-SUPPORT_SHA256=86be38660154161b6f71063567241fed3fbe1ef270d04a73b7c00a69126a9d28
+SUPPORT_SHA256=92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514
-CANARY_SHA256=0c2567320d3f5c461d0c9a4a19beaf62b83615c511c28e699810bbdfa49f3107
+CANARY_SHA256=caeb095d1e9a5f3bb631a415f49f0f039ab36874352cba43a86c0a6207440893
```

The changes bind the installer to the corrected candidate artifacts. No sudoers digest changes. Applying only these three lines to the unchanged installer yields SHA-256 `4f76bd107d6e03d276c9e3a72331fd9d1dc63337d6e72adad97ea9c0b65f861a`. **This is a provisional digest for the pin-only proposal, not an approved installer candidate:** it retains the automatic rollback defect, and any correction to that behavior will change the installer hash again. Security reviewed the cgroup code, but its disposition is BLOCKED and it has not approved this pin proposal or installer for promotion.

## Manifest reconciliation

The specified [2026-10-21 09:27 manifest](../../qualification/alpha-m1-build-reconciliation-20261021T092750Z.json), SHA-256 `9d81c2e7ccfe02472b855e653a74390ede05d10a63a380cda9f7240762018547`, matched all recorded source inputs at its captured revision and correctly recorded the old canary pin mismatch. Core's subsequent source corrections make it historical for the current candidate.

A fresh read-only [post-fix manifest](../../qualification/alpha-m1-build-reconciliation-session33-postfix.json), SHA-256 `8402cbdcc9c2a20bfad9bd9e1f6fb2596ecbf59a209605dba79fa5c0434b2e5f`, records the corrected source snapshot and the current artifact hashes without changing pins or executing cases. Its inventory results are:

| Build | Total tests | Privileged | Exact 18-case list | Artifact status |
|---|---:|---:|---|---|
| Protected `/opt/athena-qualification/build` | 140 | 18 | PASS | Protected binaries match the existing pins; source directory is `/opt/athena-qualification`, so current checkout provenance is not established. Current admin source hashes do not match those old pins. |
| Isolated Session 32 build | 147 | 18 | PASS | Artifact hashes do not match the current installer pins; this is not an approved campaign build. |

Both manifests describe their own observed snapshots. Neither proves the provenance of a protected binary from the corrected source. The updated runner/support/canary hashes need explicit pin review; the protected build and VM must be independently reconciled before campaign approval.

## Test and execution status

- **PASS:** 36 final non-privileged Python regression tests (`alpha_m1_admin_static_test`, `alpha_m1_canary_child_test`, `alpha_m1_canary_timeout_test`, `alpha_m1_orchestrator_test`, and `qualification_driver_test`).
- **PASS:** 15 gateway hardening tests.
- **PASS:** `py_compile` for canary, runner, gateway support, and focused tests.
- **FAIL:** 0 actual test-run failures.
- **SKIP:** 0 actual test-run skips.
- **TIMEOUT:** 0 actual test-run timeouts.
- **NOT RUN:** CTest; the 18 privileged cases; live systemd/cgroup canary; receipt publication on the VM; installer execution; VM process/cgroup verification. Mock orchestrator tests print simulated PASS/SKIP/TIMEOUT/infra rows; these are not campaign results.

The test suites do not establish systemd behavior on the target VM. No Alpha M1 qualification claim follows.

## D6 evidence and VM-only gates

The supplied interactive VM evidence says the fixture is root-owned `0700`, D6 residual `/var/lib/athena-alpha-m1/ext4-fixture/athena-d6-8yRDjc` contains `generations`, `projection`, `store`, and `generations/.athena-locks/state.lock`, and `/var/tmp/athena-d67-ext4` is root-owned `0700`. These are user-provided VM observations, not container observations. No agent or coordinator inspected, changed, moved, or deleted these paths. Preserve D6 in place as evidence.

Before any staging, an administrator must independently establish VM/snapshot/boot identity, enumerate descendants for the 15 failed units without inferring termination from `MainPID=0` or empty/missing cgroups, recheck fixture and mounts, inventory D6 read-only, verify installed hashes and build artifacts, validate sudoers, and resolve the installer rollback defect. After that, separate approvals are required for staging, canary/receipt, install, the 18-case campaign, and rollback. The runbook records the exact gates and stop conditions.

## Remaining blocker and next review

The installer EXIT trap invokes `rollback_all` automatically after a nonzero post-marker installation result. Authorization policy requires state-changing rollback to be a separately approved administrative action. The installer must be revised to preserve backup, marker, partial state, and diagnostic evidence without restoring targets automatically; then compute a new installer hash, update/review the relevant tests, refresh the manifest, and obtain independent Security review. Do not apply the pin-only proposal hash above or use the installer until that work is complete.

Security also notes two design limits for the next review: runner/support/sudoers swaps are individually atomic, not one transaction, so interruption can leave mixed generations; and the canary receipt path checks its final parent without walking every ancestor against symlinks. Keep the documented partial-state recovery and verify protected root-owned path components on the VM; review whether descriptor-relative ancestor walking is required before promotion. Local root-owned manifests/receipts are tamper-evident only against accidental or unprivileged changes, not a compromised root.

After a coherent installer candidate is reviewed, proceed only with the administrative read-only checks and the separately approved procedure. The outcome remains **BLOCKED** until those gates pass. Do not declare `READY FOR PRIVILEGED QUALIFICATION` or Alpha M1 qualified here.
