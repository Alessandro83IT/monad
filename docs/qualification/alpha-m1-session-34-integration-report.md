# Alpha M1 Session 34 — integration report

## Decision

**BLOCKED.** The transaction-safety correction and rootless regressions pass, but the installer still pins older runner, gateway-support, and canary hashes. Security reviewed the current installer and calculated the proposed pin-updated content hash, but the proposed patch has not been applied or reviewed as the final installer. No administrative promotion is authorized by this report.

## Parallel reviews and deliverables

- **Core:** removed automatic rollback from EXIT/signal handling; added transaction identity, durable state markers, an explicit approval-bound rollback command, incomplete-transaction refusal, locking, and best-effort partial-state recording. See [Core report](alpha-m1-session-34-core-report.md).
- **Verification:** ran the rootless static, transaction, and receipt-path suites on installer SHA-256 `157f080c4d5692af9a6f3ea8ad8b778165bd5a60c4a13b84b4cef4f0aea2c770`: **PASS 25, FAIL 0, SKIP 0, TIMEOUT 0**. `bash -n` also passed. SIGINT, SIGTERM, SIGKILL, each rename boundary, concurrent invocation, incomplete/repeated transactions, rollback refusal and approval, and receipt ancestor symlinks were exercised. See [Verification report](alpha-m1-session-34-verification-report.md).
- **Security:** independently reviewed the exact installer hash above and the candidate transaction and receipt handling. It confirmed the reported technical findings closed, but withheld administrative readiness because candidate hashes do not match the installer pins. The proposed pin-only installer content hash is `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7`; it needs to be applied through the reviewed workflow and re-reviewed with a regenerated manifest. See [Security report](alpha-m1-session-34-security-report.md).
- **Operations:** prepared separate promotion and rollback checklists and a stop/recovery procedure. The runbook remains a draft, not authorization. See [administrative runbook](alpha-m1-session-34-admin-runbook.md) and [Operations report](alpha-m1-session-34-operations-report.md).

## Candidate hashes and proposed pin diff

Container-observed SHA-256 values:

| Artifact | Current installer pin | Candidate file SHA-256 | Status |
|---|---|---|---|
| Runner | `2290b78c55c72489c0e7dc3bb1c1fedbc573e6bc605e690b6afd3a8ab326b24c` | `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` | mismatch |
| Gateway support | `86be38660154161b6f71063567241fed3fbe1ef270d04a73b7c00a69126a9d28` | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` | mismatch |
| Canary | `0c2567320d3f5c461d0c9a4a19beaf62b83615c511c28e699810bbdfa49f3107` | `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` | mismatch |
| Sudoers draft | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` | match |
| Installer, current source | — | `157f080c4d5692af9a6f3ea8ad8b778165bd5a60c4a13b84b4cef4f0aea2c770` | Security reviewed; pins stale |
| Installer, proposed pin-only content | — | `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7` | calculated only; not applied/reviewed |
| Reconciliation manifest | — | `dbb887c3c8e3269ccf8c0a4cf816fe2e46915a4d191942bb44e5fa05f25336b2` | candidate manifest |

The proposed source diff is exactly:

```diff
-RUNNER_SHA256=2290b78c55c72489c0e7dc3bb1c1fedbc573e6bc605e690b6afd3a8ab326b24c
+RUNNER_SHA256=c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def
-SUPPORT_SHA256=86be38660154161b6f71063567241fed3fbe1ef270d04a73b7c00a69126a9d28
+SUPPORT_SHA256=92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514
-CANARY_SHA256=0c2567320d3f5c461d0c9a4a19beaf62b83615c511c28e699810bbdfa49f3107
+CANARY_SHA256=9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2
```

These values were not written to the installer. The existing approved pins remain unchanged. The installer also still contains the Session 33 support-unit policy; this session did not expand gateway functionality.

## Build reconciliation

The [candidate manifest](../../qualification/alpha-m1-build-reconciliation-session34-candidate.json) was generated read-only from the existing build directories. It records:

- Protected build `/opt/athena-qualification/build`: CTest inventory **140 total / 18 privileged**, exact privileged inventory, all required build artifacts present, build artifact hashes match the current installer pins. Its source path is `/opt/athena-qualification`; source provenance to this checkout is unknown.
- Isolated build `/tmp/monad-s32-verification-final4-20261021`: **147 total / 18 privileged**, exact privileged inventory, but build artifact hashes do not match current installer pins. Its recorded source is this checkout.
- Current candidate source hashes do not match the old runner/support/canary pins, so `all_admin_hashes_match_installer_pins` is false.

These are container observations. The manifest is an observation record, not build authentication. No CTest case was run in this session; only the three targeted Python suites and shell syntax check above ran. All 18 privileged cases are **NOT RUN**.

## Safety, evidence and remaining gates

The tests establish deterministic process-level behavior in isolated, non-privileged fixtures. They do not establish root ownership behavior, actual VM receipt generation, systemd/cgroup state, ext4 crash recovery, or power-loss durability. Multiple installed-file renames are individually atomic, not one atomic transaction; an interrupted install or rollback can leave a mixed state requiring manual recovery. Approval JSON is unsigned and requires an independent human approval record. No partial recovery is treated as success.

Session 33 supplied VM observations (including 15 failed units, the fixture, the D6 residual, and the protected build) were not recollected by these agents. The D6 residual is treated as preservation evidence; no D6 path was read, changed, moved, or deleted in this session. Session 33 VM hashes are not container observations.

No `sudo`, privileged test, installation, canary, VM mutation, cleanup, commit, or push was performed. Before reconsidering readiness: apply the explicit pin proposal only through review, rerun the relevant suite on the resulting installer, regenerate and hash the manifest, obtain Security approval on that exact final installer and manifest, and complete the separate VM-side prerequisites and administrative approval in the runbook.
