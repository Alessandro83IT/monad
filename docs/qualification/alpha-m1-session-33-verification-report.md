# Alpha M1 Session 33 — Verification Report

## Scope and conclusion

Verification was limited to the specified reconciliation manifest, the current checkout's administrative source files and pins, the privileged test inventory definition, and non-privileged Python regression tests. No privileged test, installed runner, canary, sudo command, or administrative operation was invoked. No source file, manifest, installer pin, or trust anchor was changed.

**Verification result: FAIL for promotion readiness.** The candidate canary SHA-256 is `e926d2801e2923a38598f23f2a3d94d35303e2e68ee9e833c7e13b0e2a5c4592`, while the installer still pins `0c2567320d3f5c461d0c9a4a19beaf62b83615c511c28e699810bbdfa49f3107`. The manifest records this mismatch correctly. The pin must remain unchanged until an explicit reviewed promotion. The remaining manifest checks and the non-privileged tests below passed, but they do not authenticate the protected build binaries or replace VM verification.

## Checkout and evidence inspected

- Repository: `/home/alessandro/Projects/monad`; branch `main`; HEAD `abd6bac4b368427b76f6c98522afde928b418939`.
- The checkout was already dirty and contained user changes and untracked evidence before this report was written. The report is the only file changed by this verification task.
- Manifest: `qualification/alpha-m1-build-reconciliation-20261021T092750Z.json`, SHA-256 `9d81c2e7ccfe02472b855e653a74390ede05d10a63a380cda9f7240762018547`.
- Its source provenance records the same branch and HEAD. All 333 listed tracked source inputs and 23 listed untracked source inputs exist and have matching SHA-256 values in this container. Recomputing the manifest's compact, sorted JSON digest yields the recorded source snapshot digest `3f90b5f4fa635571cfda1ac65e0784ed69dac3475448acc405880ecc9a831203`.
- The manifest explicitly limits what this proves: matching source hashes and a CMake cache source path do not prove that a binary was built from this snapshot; build logs or an authenticated build record are required.

## Administrative artifact hashes observed in this container

The following hashes were computed directly from the current checkout, not copied from VM evidence:

| Artifact | Container SHA-256 | Installer pin | Result |
|---|---|---|---|
| Runner, `qualification/athena-alpha-m1-runner.py` | `2290b78c55c72489c0e7dc3bb1c1fedbc573e6bc605e690b6afd3a8ab326b24c` | `2290b78c55c72489c0e7dc3bb1c1fedbc573e6bc605e690b6afd3a8ab326b24c` | PASS |
| Gateway support, `qualification/gateway_support.py` | `86be38660154161b6f71063567241fed3fbe1ef270d04a73b7c00a69126a9d28` | `86be38660154161b6f71063567241fed3fbe1ef270d04a73b7c00a69126a9d28` | PASS |
| Canary, `qualification/athena-alpha-m1-systemd-canary.py` | `e926d2801e2923a38598f23f2a3d94d35303e2e68ee9e833c7e13b0e2a5c4592` | `0c2567320d3f5c461d0c9a4a19beaf62b83615c511c28e699810bbdfa49f3107` | FAIL: pending pin promotion |
| Sudoers draft, `qualification/athena-alpha-m1-sudoers` | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` | PASS |
| Installer, `qualification/install-athena-alpha-m1-admin.sh` | `55f13e8d1d91e149b6f24a8257fa48339f2b48e2a1b619f5ae0a5b1053d463a8` | Manifest's `installer_pin_source_sha256` | PASS |

The manifest records the same four artifact hashes and the canary pin mismatch. The user's session context reports the installed VM runner as `5d6ee35070954d97034de7f0323204995fe0ca86ed12fad774a3acb9ada021d5` and the VM candidate runner as `2290b78c55c72489c0e7dc3bb1c1fedbc573e6bc605e690b6afd3a8ab326b24c`; these remain **user-provided VM observations**, not independently verified container observations. The reported VM canary candidate hash is the same as the container candidate hash above, but this agent did not inspect the VM.

## Build and privileged inventory reconciliation

The manifest contains two build observations:

| Role | Recorded source path | CTest count | Privileged count | Manifest inventory comparison |
|---|---|---:|---:|---|
| `protected` | `/opt/athena-qualification` | 140 | 18 | PASS, matches reviewed orchestrator list |
| `isolated_updated` | `/home/alessandro/Projects/monad` | 147 | 18 | PASS, matches reviewed orchestrator list |

The protected build cache does not identify the current checkout (`source_directory_matches_checkout` is null), and its source directory is `/opt/athena-qualification`. Therefore the manifest does not establish that protected binaries correspond to the current candidate source. Its artifact hashes and inventory result are **manifest observations**; they were not independently re-read from `/opt` by this agent. The isolated build is a Session 32 path under `/tmp`; it was not rebuilt or mutated during this verification.

The exact 18 names in the manifest and `qualification/run-alpha-m1-privileged.py` are:

1. `activation_execution_privileged_sources_test`
2. `activation_execution_privileged_legacy_test`
3. `activation_execution_privileged_owner_test`
4. `activation_execution_privileged_bind_test`
5. `activation_execution_privileged_nested_test`
6. `activation_execution_privileged_ext4_test`
7. `activation_plan_retention_privileged_owner_test`
8. `activation_plan_retention_privileged_bind_test`
9. `activation_plan_retention_privileged_nested_test`
10. `activation_plan_retention_privileged_ext4_test`
11. `activation_plan_persistence_privileged_test`
12. `legacy_d3_scanner_test`
13. `activation_plan_persistence_privileged_owner_test`
14. `activation_plan_persistence_privileged_bind_test`
15. `activation_plan_persistence_privileged_nested_test`
16. `activation_plan_persistence_privileged_tmpfs_test`
17. `activation_plan_persistence_privileged_ext4_test`
18. `initial_activation_migration_test`

The current source declares those 18 tests with privileged labels and the runner/sudoers/orchestrator static alignment test confirms exactly 18 unique names. This verifies source-level inventory agreement only, not executable identity or execution on the VM.

## Non-privileged test results

Command run from the repository root:

```text
python3 -m unittest -v tests.alpha_m1_admin_static_test tests.alpha_m1_canary_child_test tests.alpha_m1_canary_timeout_test tests.alpha_m1_orchestrator_test
```

Result: **PASS 28, FAIL 0, SKIP 0, TIMEOUT 0, NOT RUN 0**. The orchestrator test cases use mocks/fake commands; their printed simulated PASS, SKIP, TIMEOUT, and infrastructure-failure rows are test fixtures, not real privileged executions. No privileged cases were run. C++ CTest cases were not run because all existing build directories were pre-existing user work and this task did not require modifying or reusing them.

## Reproducible read-only checks

From `/home/alessandro/Projects/monad`:

```text
sha256sum qualification/athena-alpha-m1-runner.py qualification/gateway_support.py qualification/athena-alpha-m1-systemd-canary.py qualification/athena-alpha-m1-sudoers qualification/install-athena-alpha-m1-admin.sh qualification/alpha-m1-build-reconciliation-20261021T092750Z.json
```

The source-input manifest verification was performed by parsing the manifest, hashing every path in `source_provenance.tracked_source_inputs` and `source_provenance.untracked_source_inputs`, and recomputing the snapshot hash as compact JSON with sorted keys over those two arrays. The non-privileged unittest command above can be rerun as written.

## VM-only items remaining

This report cannot independently verify VM identity or snapshot, systemd state, absence of descendant processes, cgroup state, installed artifact hashes, fixture/mount state, D6 residue integrity, root ownership and modes, receipt state, or build artifact provenance. The user-provided context says the D6 residual and root-owned fixture remain present; preserve that directory and its lock file as evidence. Do not treat failed units with empty cgroups as proof every descendant exited. These must be checked and documented during the approved administrative procedure before canary execution or privileged qualification.

**Status for coordinator:** manifest source hashes and inventory definitions reconcile; non-privileged tests pass; administrative candidate is not fully pin-consistent pending the explicit canary pin proposal and independent Security review. No Alpha M1 qualification claim is supported by this verification report.
