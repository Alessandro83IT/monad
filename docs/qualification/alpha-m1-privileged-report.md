# Monad Alpha M1 — rapporto di qualificazione aggiornato

> Digest e stato del runner riportati qui descrivono la revisione precedente del 2026-10-21. La revisione successiva di staging/systemd è documentata in [alpha-m1-gateway-hardening-report.md](alpha-m1-gateway-hardening-report.md); non usare i vecchi hash per promozione.

Data verifica: 2026-10-21. Esito complessivo: **INCOMPLETE**. Alpha M1 non è qualificata: il canary runtime e i 18 test privilegiati non sono stati eseguiti.

## Identità e stato del checkout

- Repository: `/home/alessandro/Projects/monad`
- Branch: `main`
- HEAD: `abd6bac4b368427b76f6c98522afde928b418939`
- Working tree: modifiche tracked e file/build non tracciati erano già presenti all'inizio della sessione; sono stati preservati. In questa sessione sono stati aggiornati solo i rapporti di qualificazione.
- Ambiente osservato: UID 1000 (`alessandro`), container con `no new privileges`; non è la VM Debian 13 verificata per il runtime systemd.

## Test non privilegiati

| Verifica | Esito |
|---|---|
| CTest completo, esclusi i test con label `privileged`, build `build-monad-debug` | **PASS** 114, **SKIP** 11, **FAIL** 0, timeout 0; 125 test selezionati, 284,06 s |
| `packaging_regression_test` mirato | **PASS**, 1/1 (4,84 s) |
| Test canary child | **PASS**, 4/4 |
| Test admin statico (allowlist, fixture, pin installer) | **PASS**, 3/3 |
| Test orchestratore (sequenza, log, mismatch inventory) | **PASS**, 2/2 |
| `qualification_driver_test.py` | **PASS**, 7/7 |
| `py_compile` per runner, canary, orchestratore e test | **PASS** |
| `bash -n qualification/install-athena-alpha-m1-admin.sh` | **PASS** |
| `git diff --check` | **PASS** |

Gli 11 SKIP di CTest sono gli scanner/integration test già configurati con return code 77; non sono stati contati come PASS:

- `installed_semantics_integration_test`
- `cli_install_integration_test`
- `install_transaction_integration_test`
- `generation_activation_test`
- `realization_executor_integration_test`
- `object_store_scanner_test`
- `artifact_identity_scanner_test`
- `removal_realization_integration_test`
- `immutable_object_publication_test`
- `transaction_gc_scanner_test`
- `package_operation_scanner_test`

Il run seriale iniziale è stato interrotto durante `activation_selection_test`; la suite completa successiva, eseguita con `-j 6`, ha completato tutti i 125 test selezionati. `activation_selection_test` ha passato in 283,92 s.

Le modifiche preesistenti a `tests/packaging_regression_test.cmake.in`, fixture, stub AppImage e inventario bounded TOML risolvono la regressione packaging presente nei rapporti storici: la prova mirata e quella nella suite completa passano. Nessuna modifica a codice prodotto è stata necessaria in questa sessione.

## Canary e copertura statica

Il canary definitivo nel checkout corrente è `qualification/athena-alpha-m1-systemd-canary.py`. Hash e pin corrispondono; `alpha_m1_canary_child_test.py` verifica compilazione del child, passaggio del percorso ext4, separazione host/privata per `/tmp` e `/var/tmp`, mount read-only dei percorsi protetti, fixture ext4 scrivibile dedicata, timeout e cleanup.

La prova statica non dimostra le proprietà runtime del namespace systemd. Il canary non è stato avviato: richiede EUID 0, invaliderebbe una receipt precedente e deve essere eseguito nella VM qualificata. Non esiste una receipt runtime verificata in questa sessione.

## Hash SHA-256

| Artefatto | SHA-256 verificato |
|---|---|
| `qualification/athena-alpha-m1-systemd-canary.py` | `5208ad30d0a55e705ac78fee205cdf6731a4f35fa13089dd0728af8cc8696cfd` |
| `qualification/run-alpha-m1-privileged.py` | `dce16f35fe8cff6659ad0229d7aa9f86fdc8e91715a3c359efd378aae9b7b23c` |
| `qualification/athena-alpha-m1-runner.py` | `5d6ee35070954d97034de7f0323204995fe0ca86ed12fad774a3acb9ada021d5` |
| `qualification/athena-alpha-m1-sudoers` | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` |
| `qualification/install-athena-alpha-m1-admin.sh` | `69a52dd41e590335d1952fdc789f985f4de03ef08ced885dd87ada0bd34a3adf` |

I tre pin del runner, sudoers e canary dichiarati dall'installer coincidono con i file correnti. I sei pin interni del runner coincidono con gli SHA-256 osservati in `/opt/athena-qualification/build`:

| Artefatto autenticato | SHA-256 |
|---|---|
| `activation_execution_privileged_test` | `d87a84cbb1e18ba073beaadb1bb7737b61fa448081af37e4d4c5a6c29337d157` |
| `activation_plan_retention_test` | `f76251d6e0af0bd892d1dcb17f41f80b13343c7c274e289014dfcb090370e474` |
| `activation_plan_persistence_test` | `f12620d89bb55e66f1e1960dd8852f2fdf27c9a148967d222d562fdddbd5aaa3` |
| `legacy_d3_test` | `61e23e8c9cd3a81c8766eb61e93e01cc7c9af2025a80f6056f28d3ac137ad6a9` |
| `initial_activation_test` | `934222dc181ae1a98a3d00bed5cba8e208cdad194d0c10fe552b1e5cf7e74243` |
| `libathena_core.so` | `e212420cb34aeca76cebe2ecf396832f25c3c4f119b846b42ef00d2994cc995a` |

Il build locale `build-monad-debug` è distinto dal build autenticato: i suoi binari non corrispondono ai pin del runner e non va usato per la campagna privilegiata.

## Orchestratore, inventario e log

L'orchestratore verifica l'inventario JSON CTest prima di chiamare sudo. `ctest --show-only=json-v1` sul build autenticato `/opt/athena-qualification/build` ha mostrato 140 test totali e gli esatti 18 casi privilegiati previsti, nell'ordine della allowlist. I test statici simulano tutti i 18 risultati, verificano esecuzione sequenziale, raccolta output e continuazione dopo un fallimento, nonché rifiuto dell'inventario inatteso.

Ogni invocazione effettiva sarebbe `sudo -n /usr/local/sbin/athena-alpha-m1-runner CASE`. Il runner registra JSON e output limitato sotto `/var/log/athena-alpha-m1`; l'orchestratore crea log individuali, `results.json` e `results.tsv` in `qualification/alpha-m1-privileged-results/<UTC>/`, conserva ogni exit code e continua dopo errori. Exit code 0 è PASS; exit nonzero è FAIL; timeout è TIMEOUT; un 77 del binario non viene promosso a PASS. L'orchestratore restituisce 0 solo con 18 PASS, 1 con esiti negativi/timeout e 2 se l'inventario non corrisponde.

## Stato amministrativo e test privilegiati

Runner installato: **NO** (`/usr/local/sbin/athena-alpha-m1-runner` assente). Regola sudoers installata: **NO** (`/etc/sudoers.d/athena-alpha-m1` assente). `sudo -n` non ha funzionato: il container segnala `/etc/sudo.conf` non posseduto da root e `no new privileges`. Non sono state richieste né usate password. Non è stato invocato l'installer e non sono stati modificati percorsi protetti.

| Caso privilegiato | Esito corrente |
|---|---|
| `activation_execution_privileged_sources_test` | **NOT RUN** |
| `activation_execution_privileged_legacy_test` | **NOT RUN** |
| `activation_execution_privileged_owner_test` | **NOT RUN** |
| `activation_execution_privileged_bind_test` | **NOT RUN** |
| `activation_execution_privileged_nested_test` | **NOT RUN** |
| `activation_execution_privileged_ext4_test` | **NOT RUN** |
| `activation_plan_retention_privileged_owner_test` | **NOT RUN** |
| `activation_plan_retention_privileged_bind_test` | **NOT RUN** |
| `activation_plan_retention_privileged_nested_test` | **NOT RUN** |
| `activation_plan_retention_privileged_ext4_test` | **NOT RUN** |
| `activation_plan_persistence_privileged_test` | **NOT RUN** |
| `legacy_d3_scanner_test` | **NOT RUN** |
| `activation_plan_persistence_privileged_owner_test` | **NOT RUN** |
| `activation_plan_persistence_privileged_bind_test` | **NOT RUN** |
| `activation_plan_persistence_privileged_nested_test` | **NOT RUN** |
| `activation_plan_persistence_privileged_tmpfs_test` | **NOT RUN** |
| `activation_plan_persistence_privileged_ext4_test` | **NOT RUN** |
| `initial_activation_migration_test` | **NOT RUN** |

Non sono stati ispezionati o modificati fixture, receipt, mount o snapshot della VM, perché l'ambiente disponibile è un container e le directory di stato sono root-owned/inaccessibili all'utente corrente.

## Unico intervento umano residuo

Un amministratore deve completare una sessione nella VM Debian 13 qualificata: verificare che il build autenticato sia quello corretto; copiare in `/root/athena-alpha-m1-stage` i tre file runner, sudoers e canary con gli hash riportati; eseguire `prepare-fixture`; avviare il canary con `--record`; eseguire l'installer con un percorso backup univoco; validare sudoers con `visudo -c`. La procedura controllabile e i comandi esatti sono in [alpha-m1-admin-staging.md](alpha-m1-admin-staging.md). Quindi `alessandro` può lanciare, senza privilegi, l'orchestratore:

```sh
python3 qualification/run-alpha-m1-privileged.py --build-dir /opt/athena-qualification/build
```

Conservare gli artefatti di risultato e i log root, verificare tutti i 18 PASS e il cleanup della fixture. Qualunque SKIP, errore sudo, timeout o esito nonzero lascia la qualificazione incompleta.
