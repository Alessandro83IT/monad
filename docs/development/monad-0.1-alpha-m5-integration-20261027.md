# Monad 0.1 Alpha M5 — integrazione non privilegiata

**Decisione: NO-GO. Alpha M1 resta BLOCKED.** M5 completa verifiche locali
mirate su selezione e persistenza del piano e riesegue la suite pertinente non
privilegiata. Le prove usano build e fixture locali; non qualificano
l'installazione reale, il filesystem Debian dopo blackout o i casi privileged.

## Perimetro e provenienza

- Data: 2026-10-27. Branch `main`; `HEAD` prima e dopo: `abd6bac4b368427b76f6c98522afde928b418939`.
- Il worktree era già ampiamente modificato e conteneva rapporti, fixture e build non tracciati. Le modifiche esistenti sono state preservate. In M5 è stato aggiunto solo questo rapporto.
- Build Debug nuova e isolata: `/tmp/monad-m5-build` (GCC 14.2.0, OpenSSL 3.5.7, LibArchive 3.7.4, Python 3.13.5). Nessun output di build è stato scritto nelle directory preesistenti.
- Nessun reset/clean, commit/push, installazione, accesso a credenziali, test privileged, scanner, collector Alpha M1 o driver host Sessione 68. Baseline, inventari ufficiali, pin e fixture D6 non sono stati modificati.

Sono stati letti integralmente i rapporti M1, M2, M3 e M4 in `docs/development/`.
La ricostruzione dei requisiti e dei blocchi è:

| Area | Requisito/evidenza cumulativa M1–M4 | Stato all'avvio M5 |
|---|---|---|
| CLI, metadata, resolver, download, build, store legacy | M1/M2 regressioni shell injection, identity/path, archive/store; M3 estrazione LibArchive e pubblicazione legacy NOREPLACE | Implementato; verifiche locali, non installazione reale. Strictness legacy e trust dei build script ancora aperti. |
| Oggetti immutabili e generazioni | Pubblicazione durevole locale; generazione separata dalla selezione | Implementato; integrazioni scanner-dependent non disponibili nel perimetro non privilegiato. |
| Selection, rollback e reconciliation | M4 descrive record current separato, recovery base/target e reconciliation fail-closed/idempotente | `activation_selection_test` e i quattro checkpoint `activation_plan_persistence_boundaries_*` non avevano esito affidabile o erano NOT RUN. |
| Recovery store legacy | M4 aggiunge ispezione read-only degli stage e test SIGKILL | Verificato solo per crash di processo; nessuna pulizia/adoption automatica e nessuna prova blackout. |
| Source inventory e identità candidato | M1/M2 riportano mismatch dell'inventario ufficiale e pin che richiedono promozione separata | BLOCKED; vietato modificare baseline o pin. |
| Qualificazione M1 | Campagna Debian approvata, casi privileged, scanner fisico ed evidence review | BLOCKED / NOT RUN. |

## Matrice delle transizioni

Le transizioni seguenti sintetizzano l'implementazione esaminata e le prove
locali. “Coperta” indica test su fixture non privilegiata, non qualificazione
operativa sul sistema installato.

| Operazione/transizione | Invarianti attese | Evidenza M5 | Residuo |
|---|---|---|---|
| Installazione: assente → generazione target | Preflight e realization prima della pubblicazione; oggetti/generazione non sovrascritti; selezione distinta dalla pubblicazione | PASS: `install_transaction_test`, `installer_e2e_test`, `generation_builder_test`, test package operation nella suite | L'integrazione con scanner/proiezione reale è privileged e NOT RUN. |
| Attivazione: generazione pubblicata → current | Current identifica piano/digest validi; un errore di preparazione non seleziona target; selezione serializzata | PASS: `activation_selection_test` (include fault injection, processi concorrenti e SIGKILL); `activation_projection_test`; `activation_reconciliation_test` | Nessuna prova power-loss su filesystem target. |
| Aggiornamento: current A → nuova generazione B | A resta base riconoscibile durante la preparazione; retry esatto è idempotente; conflitti di piani diversi falliscono | PASS: scenari di commit/retry/concorrenza in `activation_selection_test`; transaction/generation unit tests | Nessuna transazione end-to-end con scanner privileged. |
| Rimozione: membership corrente → membership senza pacchetto | Pubblicazione di nuova generazione; i dati condivisi sono protetti da membership/pin; errore non autorizza cancellazione ricorsiva | PASS: `remove_plan_test`, `removal_planner_test`, `removal_e2e_test`, `removal_realization_test`, `transaction_gc_*` non scanner | `removal_realization_integration_test` non è stata eseguita. |
| Switch: generazione esistente → altra generazione valida | La selezione non inventa generazioni; destinazione verificata e current aggiornato con protocollo durevole | PASS: `activation_selection_test` (selezione 0→1→0), `generation_test`, `generation_membership_test` | Verifica su root installata/privileged NOT RUN. |
| Rollback: current → generazione precedente | Rollback è una nuova selezione verso generazione pubblicata, non cancellazione né ricostruzione implicita della cronologia | PASS: transizione 1→0, retry e recovery in `activation_selection_test`; `activation_test` | Storia operativa persistente e VM non qualificate. |
| Recovery selection | Riconosce base oppure target dal record current e dal piano; piano pendente non viene promosso implicitamente; stato ambiguo fallisce chiuso | PASS: `activation_selection_test`, `activation_plan_persistence_test`, `activation_plan_persistence_recovery-*`, quattro boundary test; fault e SIGKILL | Blackout, cache del device, journal replay e reboot NOT RUN. |
| Recovery store legacy | Stage con solo nome non è prova di completezza/proprietà; ispezione non segue, modifica o rimuove residui | PASS precedente M4: fixture SIGKILL e `store_metadata_test`; suite M5 rilancia `store_metadata_test` | Residui crash richiedono review manuale; durabilità legacy priva di prova. |
| Ripetizione delle operazioni | Retry esatto non duplica o sovrascrive; retry dopo esito incerto riconosce base/target; ripetizione conflittuale è rifiutata | PASS: selection retry, fault injection, `activation_plan_persistence_*`, store collision e transaction/GC tests | Semantica operativa intera install/remove non testata sotto scanner privilegiato. |

## Difetti e modifiche

M5 non ha osservato un nuovo difetto funzionale nelle transizioni provate e non
ha modificato codice o test. Non sono state introdotte modifiche ai test per
mascherare risultati. È stato riprodotto il blocco noto di source closure:
`qualification_driver_test` fallisce perché l'inventario ufficiale non elenca
13 input presenti: `src/process/run.cpp`, `src/process/run.hpp`, nove test
Alpha M1 già noti, `tests/process_invocation_test.cpp` già noto da M2 e il nuovo
`tests/archive_security_test.cpp` rispetto all'inventario. Il totale dei test
non elencati è quindi dieci storici più il nuovo test archive.
Il dettaglio esatto restituito dal driver è conservato in
`/tmp/monad-m5-ctest.log`. L'inventario e il candidato non sono stati riscritti.

## Build e test

Configurazione e build completa Debug in `/tmp/monad-m5-build`: **PASS**.
La selezione CTest del set non privilegiato comprende 109 casi. I casi
scanner/privileged/diagnostic, tooling `alpha_m1_*` e i cinque checkpoint già
lanciati separatamente sono stati esclusi prima della suite larga. Non è stato
lanciato alcuno scanner non autorizzato.

| Verifica | Esito | Dettaglio |
|---|---|---|
| Debug configure | **PASS** | `cmake -S . -B /tmp/monad-m5-build -DCMAKE_BUILD_TYPE=Debug` |
| Build completa | **PASS** | `cmake --build /tmp/monad-m5-build -j2`; warning `nodiscard` in `store_metadata_errors_test.cpp`, non bloccante e preesistente. |
| Suite non privileged | **108 PASS, 1 FAIL, 0 SKIP, 0 TIMEOUT** | 109 selezionati; log completo `/tmp/monad-m5-ctest.log`. FAIL: `qualification_driver_test`, errore setup per input non elencati. |
| `activation_selection_test` | **PASS** | CTest isolato, `--overwrite Timeout=300`; 234.06 s. Include 16 fault boundary fsync, 141 open, 5.529 stat, 90 pread, più processi concorrenti e SIGKILL. |
| `activation_plan_persistence_boundaries_fresh_test` | **PASS** | 90.30 s; CTest isolato, timeout 120 s. |
| `activation_plan_persistence_boundaries_bootstrap_test` | **PASS** | 93.20 s; CTest seriale, timeout 120 s. |
| `activation_plan_persistence_boundaries_reused_test` | **PASS** | 83.15 s; CTest seriale, timeout 120 s. |
| `activation_plan_persistence_boundaries_published_test` | **PASS** | 58.04 s; CTest seriale, timeout 120 s. |
| Esecuzione esplorativa selection con limite 60 s | **TIMEOUT** | Invocazione diretta terminata da `timeout`, exit 124, nessun output. Successivamente il caso è stato completato da CTest in 234.06 s. Non è stato contato come esito della suite finale. |
| `git diff --check` | **PASS** | Nessun errore whitespace. |
| Privileged, diagnostic, scanner, integrazioni scanner-dependent | **NOT RUN** | Escluse prima dell'invocazione. |
| Source inventory, pin promotion, blackout/VM, collector, driver Sessione 68 | **NOT RUN** | Fuori perimetro e/o gate separati; nessun baseline/pin alterato. |

La suite larga registrava 149 test totali e ne ha selezionati 109. I cinque
checkpoint mirati (selection e quattro persistence boundaries) sono stati
eseguiti a parte e tutti hanno PASS. Aggregando le esecuzioni finali pertinenti:
**113 PASS, 1 FAIL, 0 SKIP, 0 TIMEOUT**. Il TIMEOUT esplorativo di 60 secondi è
riportato separatamente e non annulla il successivo PASS completo.

## Blocchi residui di qualificazione

- **Source inventory:** il driver ufficiale continua a rifiutare il worktree
  per input non elencati; il candidato M2 è un artefatto separato e non una
  promozione. La chiusura richiede review dell'intero source set e workflow
  approvato. M5 non modifica inventari storici.
- **Pin:** le modifiche sorgente preesistenti cambiano l'identità del candidato.
  Nessun pin è stato rigenerato o promosso; occorre il workflow di promozione
  revisionato, con hash e build candidato riesaminati.
- **Durabilità dopo blackout:** i test coprono errori controllati e terminazione
  di processo, non perdita alimentazione, storage cache, reboot, replay del
  journal o filesystem Debian. Le barriere fsync del protocollo non sostituiscono
  una campagna fisica.
- **Verifiche VM:** non sono stati eseguiti Debian VM, casi privileged, scanner
  fisico né evidence review. Restano obbligatori per chiudere Alpha M1.
- **Altri rischi:** strictness dello schema legacy, esecuzione dei build script
  con autorità dell'utente, residui stage legacy conservativi e atomicità
  distribuita fra store/pin/generazioni/current restano limiti documentati in
  M1–M4.

## Decisione

**NO-GO; Alpha M1 resta BLOCKED.** I checkpoint non privilegiati richiesti sono
ora PASS e la suite locale è ampiamente verde, ma il FAIL dell'inventario,
l'assenza di promozione pin, la mancanza di prova blackout e la campagna VM
privileged non eseguita impediscono una decisione GO o una qualifica Alpha M1.
