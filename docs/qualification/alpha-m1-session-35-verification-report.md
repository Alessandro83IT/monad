# Alpha M1 Session 35 — verification report

## Scope and environment

**Disposition: BLOCKED.** This acquisition ran in the development container, not in the dedicated Debian qualification VM. The container reports Debian GNU/Linux 13.7, kernel `6.12.113+deb13-amd64`, `x86_64`, and `systemd-detect-virt` reports `container-other`. `systemd-detect-virt --vm` returned `oracle`, which is not sufficient to identify a qualification VM from inside this container. `/proc/1/cgroup` is `0::/user.slice/user-1000.slice/session-4.scope`. No VM snapshot interface or snapshot identifier was available.

Read-only identity observations at `2026-10-21T11:23:42Z`:

- `/etc/machine-id`: `9bdbfd8678b5467cb85e4d8aa5e01fea` (container-visible identity; not asserted as VM identity).
- `/proc/sys/kernel/random/boot_id`: `3fec68c2-2d94-45ea-b574-00249796f987` (container-visible boot identity; not asserted as VM identity).
- The report-generation acquisition recorded `systemd --version` as `257 (257.13-1~deb13u1)`, but a coordinator-side repeat found no `systemd` executable (`systemd --version`: command not found). The systemd version is therefore **BLOCKED / not independently reproducible** and must not be treated as established VM evidence.
- `systemctl is-system-running` could not connect to the system scope bus (`Operation not permitted`), exit 1. Unit inventory and VM logs therefore remain **NOT RUN** here.
- Effective identity was `uid=1000(alessandro) gid=1000(alessandro)`; no elevated access was used.

## Candidate identity

The checked out candidate matched the Session 34B values:

| Item | SHA-256 | Result |
|---|---|---|
| Installer `qualification/install-athena-alpha-m1-admin.sh` | `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7` | PASS, matched |
| Manifest `qualification/alpha-m1-build-reconciliation-session34b-candidate.json` | `00fd4036bf0f3873f5c2a0e932c912b4709460fa52c35a1c2564016a92811262` | PASS, matched |
| Runner `qualification/athena-alpha-m1-runner.py` | `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` | PASS, matched |
| Gateway support `qualification/gateway_support.py` | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` | PASS, matched |
| Canary `qualification/athena-alpha-m1-systemd-canary.py` | `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` | PASS, matched |
| Sudoers draft `qualification/athena-alpha-m1-sudoers` | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` | PASS, matched |

The checkout was on branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`. It had pre-existing tracked modifications and untracked evidence before this report was created. Those edits were preserved. This report does not claim the container checkout is the VM candidate.

## CTest build inventory and provenance

Method: `ctest --test-dir BUILD --show-only=json-v1`; JSON test properties were parsed for `LABELS=privileged`. No CTest test was executed.

| Build | Readability and observed generation data | Inventory | Provenance result |
|---|---|---|---|
| `/opt/athena-qualification/build` | Readable; Ninja; Debug; source cache `/opt/athena-qualification`; CMakeCache SHA-256 `25ad2bffee04377776021a44efcd933845f489e52462b025175cb6928baaeb21`; `build.ninja` SHA-256 `f47470bf0b6cb5062f4d99cc81f71c3ac325686f4640ac160a1d8fc7fc90d86a` | 140 total; 18 privileged | **BLOCKED:** cache identifies a separate source path. No authenticated source revision, source snapshot, build-input manifest, or artifact-to-source relationship was available. |
| `/tmp/monad-s32-verification-final4-20261021` | Readable; Ninja; Debug; source cache `/home/alessandro/Projects/monad`; CMakeCache SHA-256 `8fc4ce3e6e4cb5203359dfc7244d736cf2993b3ed8279badec0657abb7e5a98e`; `build.ninja` SHA-256 `09e29110681322acce47c568f1c1d026f72f2de74815f24fda7dc21f42c3564a` | 147 total; 18 privileged | **BLOCKED:** cache source path points to this checkout but does not bind the binaries to the current HEAD or candidate snapshot. Checkout modifications also prevent inferring a clean source state. |

Both build directories were present and their CTest JSON commands exited 0. Their exact privileged-labeled inventory was identical:

```text
activation_execution_privileged_sources_test
activation_execution_privileged_legacy_test
activation_execution_privileged_owner_test
activation_execution_privileged_bind_test
activation_execution_privileged_nested_test
activation_execution_privileged_ext4_test
activation_plan_retention_privileged_owner_test
activation_plan_retention_privileged_bind_test
activation_plan_retention_privileged_nested_test
activation_plan_retention_privileged_ext4_test
activation_plan_persistence_privileged_test
legacy_d3_scanner_test
activation_plan_persistence_privileged_owner_test
activation_plan_persistence_privileged_bind_test
activation_plan_persistence_privileged_nested_test
activation_plan_persistence_privileged_tmpfs_test
activation_plan_persistence_privileged_ext4_test
initial_activation_migration_test
```

The difference between 140 and 147 total tests remains unresolved by this inspection. The prior Session 34B reconciliation records the same totals and correctly states that cache paths do not prove build provenance. Because the protected build's provenance is not demonstrated, its gate is **NOT SATISFIED**. Proposed controlled next step for a later reviewed session: preserve both existing build trees, independently identify and authenticate a clean candidate source snapshot and toolchain, then configure and build into a new unique isolated directory and record source revision, dirty-state status, compiler/tool versions, full build logs, and hashes of resulting test executables and generated inventory. This reconstruction was **NOT RUN**.

## Non-privileged regressions

Command, from repository root:

```sh
bash -n qualification/install-athena-alpha-m1-admin.sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v \
  tests.alpha_m1_admin_static_test \
  tests.alpha_m1_installer_transaction_test \
  tests.alpha_m1_receipt_path_test
```

Result: `bash -n` **PASS**; unit suite **PASS 25, FAIL 0, SKIP 0, TIMEOUT 0** (`Ran 25 tests in 8.711s — OK`). The suite uses non-privileged local test fixtures. The main exercised object is the installer SHA-256 above. This confirms the Session 34B static/transaction/receipt-path suite on this checkout; it does not establish VM permissions, real root ownership, filesystem crash durability, or the build provenance described above.

## Outcome categories and limits

- **PASS:** candidate installer and manifest hashes match Session 34B; build directories are readable; both CTest JSON inventories parsed; exact same 18 privileged labels enumerated; installer shell syntax check; 25 non-privileged Session 34B tests.
- **FAIL:** 0 observed in the checks that ran.
- **SKIP:** 0.
- **TIMEOUT:** 0.
- **BLOCKED:** actual VM identity/snapshot cannot be established in this container; VM systemd bus and unit/process/cgroup evidence unavailable; protected build provenance not demonstrated; isolated build provenance not demonstrated.
- **NOT RUN:** any CTest case (including all 18 privileged tests), VM systemd/log/process/cgroup acquisition, VM filesystem/mount/receipt/ACL acquisition, installation, live canary, rollback, or build reconstruction.

No privileged tests, `sudo`, administrative installation, live canary, rollback, systemd state change, or filesystem mutation was performed. This report is a container-side verification record and does not qualify Alpha M1 or establish readiness for staging approval.
