# Alpha M1 — rapporto sessione 30

> Rapporto storico: i digest e le procedure sono riferiti alla sessione indicata e non al runner attuale. Per la revisione successiva, consultare [alpha-m1-gateway-hardening-report.md](alpha-m1-gateway-hardening-report.md).

## Verdetto: INCOMPLETE

Questa sessione ha completato verifiche non privilegiate, controllo read-only della build autenticata e audit statico dei file di staging. Non ha eseguito il canary, installato runner o sudoers, né eseguito test privilegiati o scanner. Il contesto corrente non è la VM VirtualBox richiesta e non dispone dei prerequisiti amministrativi. Nessun risultato Athena storico viene attribuito al commit Monad `abd6bac`.

## Repository e ambiente osservati

- Checkout `/home/alessandro/Projects/monad`, branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`.
- Working tree preesistente: quattro file modificati (`tests/bounded_toml_sources.txt`, `tests/packaging_appimage_tool_stub.sh`, `tests/packaging_fixture/CMakeLists.txt`, `tests/packaging_regression_test.cmake.in`) e artefatti non tracciati `build-alpha/`, `build-monad-appimage/`, `build-monad-debug/`, `build-monad-release/`, `qualification/`, più i rapporti già presenti. Tutti preservati.
- Debian GNU/Linux 13.7 (trixie); `systemd-detect-virt --vm` identifica `oracle`, non VirtualBox.
- `/` e `/var/tmp`: ext4, read-only nel namespace osservato.
- `systemctl is-system-running` non raggiunge il bus di sistema (`Operation not permitted`). `visudo` è assente. `getfacl` e `systemd-run` esistono, ma non attestano systemd funzionante.
- UID 1000 `alessandro`, nessun privilegio elevato dimostrato. Nessuna password richiesta o usata.
- `/var/tmp/athena-d67-ext4` è `nobody:nogroup`, modo `0700`, ACL minimale. Non è la fixture root-owned richiesta e non è stata alterata.

## Build autenticata

`/opt/athena-qualification/build` è stata verificata in sola lettura. Tutti e sette gli hash coincidono con quelli dello script di installazione:

| Artefatto | SHA-256 |
|---|---|
| `activation_execution_privileged_test` | `d87a84cbb1e18ba073beaadb1bb7737b61fa448081af37e4d4c5a6c29337d157` |
| `activation_plan_retention_test` | `f76251d6e0af0bd892d1dcb17f41f80b13343c7c274e289014dfcb090370e474` |
| `activation_plan_persistence_test` | `f12620d89bb55e66f1e1960dd8852f2fdf27c9a148967d222d562fdddbd5aaa3` |
| `legacy_d3_test` | `61e23e8c9cd3a81c8766eb61e93e01cc7c9af2025a80f6056f28d3ac137ad6a9` |
| `initial_activation_test` | `934222dc181ae1a98a3d00bed5cba8e208cdad194d0c10fe552b1e5cf7e74243` |
| `libathena_core.so` | `e212420cb34aeca76cebe2ecf396832f25c3c4f119b846b42ef00d2994cc995a` |
| `CTestTestfile.cmake` | `99751015487882864d67b948dbbb0eee4ac7204aca1b7ee20435f8e69b656bd5` |

The build and sampled components are `nobody:nogroup`, mode `0550` or stricter; files are `0555`/`0444`. ACLs grant `alessandro` read/execute only and have no write bits. No build, binary, ownership, mode, or ACL changed. This establishes the hashes in the visible mount, not the integrity of a distinct VM copy. `/opt` itself is UID 65534 owned and non-writable here; the full VM path/ACL state remains unverified.

## Audit statico

Reviewed fully: `qualification/install-athena-alpha-m1-admin.sh`, `qualification/athena-alpha-m1-runner.py`, `qualification/athena-alpha-m1-systemd-canary.py`, `qualification/athena-alpha-m1-sudoers`, and `docs/qualification/alpha-m1-admin-staging.md`.

The stage hashes agree with documentation and installer constants: runner `4fa45103be011f3dd2250313d1e36eb02b2a7979b70f1bf4a546742a9b926568`, canary `d4fe7b1bf25496cb750d05b6a1c11291478f595a07d5556cc60105f29b3908db`, sudoers `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18`.

The sudoers draft lists fixed runner paths with one fixed test argument each, no wildcards or shell; all 18 arguments match the runner allowlist. The runner validates the pinned executable and library hashes, protected directories and fixture, invokes bounded systemd transient units, and bounds audit logs. The installer checks staging hashes and metadata, requires a fresh matching canary receipt, backs up prior destinations, installs via temporary same-directory files, and attempts independent rollback while retaining its journal. Static review found no arbitrary sudo argument path or obvious missing rollback branch. Runtime systemd isolation, sudoers syntax and rollback remain unverified. `visudo` and `shellcheck` are unavailable. No repository source or pinned hash needed correction.

## Canary and 18 privileged tests

Canary: **NOT EXECUTED**, no receipt written. Administrative staging, sudoers validation and runner installation: **NOT EXECUTED**. No system administrative changes were made, so restoration was not applicable. Each of the 18 tests is **NOT EXECUTED in session 30**; commands, durations and logs are not applicable, and no PASS is inferred.

| Test | Status |
|---|---|
| `activation_execution_privileged_sources_test` | NOT EXECUTED |
| `activation_execution_privileged_legacy_test` | NOT EXECUTED |
| `activation_execution_privileged_owner_test` | NOT EXECUTED |
| `activation_execution_privileged_bind_test` | NOT EXECUTED |
| `activation_execution_privileged_nested_test` | NOT EXECUTED |
| `activation_execution_privileged_ext4_test` | NOT EXECUTED |
| `activation_plan_retention_privileged_owner_test` | NOT EXECUTED |
| `activation_plan_retention_privileged_bind_test` | NOT EXECUTED |
| `activation_plan_retention_privileged_nested_test` | NOT EXECUTED |
| `activation_plan_retention_privileged_ext4_test` | NOT EXECUTED |
| `activation_plan_persistence_privileged_test` | NOT EXECUTED |
| `activation_plan_persistence_privileged_owner_test` | NOT EXECUTED |
| `activation_plan_persistence_privileged_bind_test` | NOT EXECUTED |
| `activation_plan_persistence_privileged_nested_test` | NOT EXECUTED |
| `activation_plan_persistence_privileged_tmpfs_test` | NOT EXECUTED |
| `activation_plan_persistence_privileged_ext4_test` | NOT EXECUTED |
| `legacy_d3_scanner_test` | NOT EXECUTED |
| `initial_activation_migration_test` | NOT EXECUTED |

The earlier privileged report lists five operator-reported PASS and 13 not executed on Athena commit `4b2d4719665fbcbb16b6c68b0ee2d0914c01fe6f`. Those are historical Athena results, not evidence for Monad `abd6bac`.

## 11 scanner integrations

The retained CTest diagnostic from 2026-10-20 recorded 11 SKIP (CTest code 77), attributed to the lack of `CAP_SYS_ADMIN` needed to verify hidden `trusted.*` xattrs. This was an Athena baseline run and was not repeated on Monad HEAD.

| Test | Recorded reason |
|---|---|
| `installed_semantics_integration_test` | production scanner cannot establish absence of `trusted.*` xattrs |
| `cli_install_integration_test` | same scanner capability requirement |
| `install_transaction_integration_test` | same scanner capability requirement |
| `generation_activation_test` | same scanner capability requirement |
| `realization_executor_integration_test` | same scanner capability requirement |
| `artifact_identity_scanner_test` | same scanner capability requirement |
| `object_store_scanner_test` | same scanner capability requirement |
| `immutable_object_publication_test` | complete ArtifactId publication requires complete scanner metadata inspection |
| `removal_realization_integration_test` | same production-scanner requirement |
| `transaction_gc_scanner_test` | same production-scanner requirement |
| `package_operation_scanner_test` | physical B4 source authorization requires production scanner |

They may be runnable in the target VM if the actual test process has the needed trusted-xattr inspection capability and fixture setup succeeds. That has not been established here, so none were retried and no skip condition was bypassed.

## Stato finale, prove e ripresa operativa

No mounts, fixture contents, sudoers, installed runner or administrative state were changed. Existing qualification artefacts were preserved. Session 30 created no runtime test logs. Existing Athena evidence remains under `qualification/alpha-m1-diagnostic-20261020/`, `docs/qualification/alpha-m1-ctest-diagnostic-20261020.md` and `docs/qualification/alpha-m1-privileged-report.md`.

Per riprendere, l’operatore deve accedere alla VM VirtualBox verificata e autorizzare esplicitamente installazione e prove. In una shell root sulla VM: verificare identità, systemd, filesystem, `visudo`, `getfacl`, i sette hash, ownership/ACL e fixture; copiare i tre file di staging revisionati come indicato in `docs/qualification/alpha-m1-admin-staging.md`; quindi eseguire l’installer revisionato dal checkout:

```sh
/path/to/reviewed/qualification/install-athena-alpha-m1-admin.sh prepare-fixture
/usr/bin/python3 /root/athena-alpha-m1-stage/athena-alpha-m1-systemd-canary.py --record
/path/to/reviewed/qualification/install-athena-alpha-m1-admin.sh install \
  /var/lib/athena-alpha-m1/backups/run-20261021-<unique-time>
```

Dopo canary valido, verifica installazione e autorizzazione operatore, eseguire ciascun caso singolarmente e conservare i JSON e `.out`:

```sh
sudo -n /usr/local/sbin/athena-alpha-m1-runner <exact-CTest-name>
```

Dopo le prove e la verifica del cleanup, ripristinare lo stato amministrativo precedente con lo stesso percorso di backup:

```sh
/path/to/reviewed/qualification/install-athena-alpha-m1-admin.sh rollback \
  /var/lib/athena-alpha-m1/backups/run-20261021-<unique-time>
```

Fermarsi in caso di canary non valido, hash mismatch, rollback parziale, fixture non vuota o mount inatteso. Verdetto attuale: **INCOMPLETE**.
