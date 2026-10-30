# Monad 0.1 Alpha M4 — recovery e integrità transazionale

**Esito: NO-GO. Alpha M1 resta BLOCKED.** M4 aggiunge un’osservazione conservativa degli stage legacy rimasti nello store e una regressione con terminazione SIGKILL. Non promuove il candidato e non qualifica durabilità dopo perdita di alimentazione.

## Perimetro e provenienza

Sono stati letti integralmente i rapporti M1, M2 e M3 in `docs/development/`. Il checkout iniziale era già modificato e conteneva build, fixture, rapporti e strumenti non tracciati. Branch `main`; `HEAD` iniziale e finale: `abd6bac4b368427b76f6c98522afde928b418939`. Le modifiche già presenti sono state preservate. Build isolata: `/tmp/monad-m4-recovery-build`.

Nessun reset/clean, commit/push, installazione reale, collector Alpha M1, driver host Sessione 68, scanner non autorizzato o test privilegiato è stato eseguito. Baseline, inventari ufficiali, pin, fixture D6 e rapporti storici non sono stati modificati. Nessuna credenziale è stata letta.

## Comportamento ricostruito e recovery

### Download, staging, copia e pubblicazione legacy

Il percorso legacy scarica l’archivio nella cache, verifica SHA-256, estrae in una directory unica privata, compila con logica fornita dal pacchetto in un albero di staging e copia i file regolari in uno stage privato fratello della destinazione store. Il manifest e i metadati sono scritti nello stage; la pubblicazione avviene con `renameat2(RENAME_NOREPLACE)`. Una destinazione esistente non viene sostituita.

| Punto di interruzione | Crash del processo | Errore recuperabile | Perdita improvvisa di alimentazione |
|---|---|---|---|
| Download/verifica | Un archivio incompleto può restare nella cache; non esiste prova che sia completo finché SHA-256 non è verificato. | Errore di lettura/hash interrompe il flusso; nessuna installazione viene pubblicata da questo passaggio. | Persistenza e stato della cache non sono sincronizzati né qualificati come durevoli. |
| Estrazione | La directory privata può restare orfana. Gli errori catturati rimuovono solo la directory creata dall’operazione. | Input rifiutato/errore parser abortisce senza adottare output parziale. | Nessuna barriera e nessuna verifica di reboot; possibili residui o namespace non persistito. |
| Copia nello stage store | `.monad-store-stage-*` può restare parziale e senza proprietario verificabile. | Le eccezioni nel processo rimuovono lo stage operation-owned; la destinazione finale resta assente. | Nessuna fsync: non è stabilito cosa sopravvive o se l’ordine dei dati/nomi sia preservato. |
| Verifica e metadati nello stage | L’interruzione può lasciare uno stage con contenuto apparentemente completo ma non pubblicato. Il nome non prova completezza o provenienza. | Errori di scrittura impediscono il rename; cleanup locale best-effort. | Non si può distinguere in modo affidabile stage completo da stage parziale dopo reboot. |
| Pubblicazione | Il rename è atomico rispetto alla visibilità: si osserva il vecchio stato o la directory completa. Un crash può lasciare la destinazione pubblicata oppure lo stage. | `EEXIST` fallisce senza sovrascrivere la destinazione; gli altri errori abortiscono. | Senza fsync di file e directory, la visibilità osservata prima del blackout non prova persistenza. L’esito è non qualificato/uncertain. |

**Correzione M4:** `store::inspect_recovery_candidates()` enumera in sola lettura i nomi `.monad-store-stage-*` e restituisce percorso e tipo senza seguirli o modificarli. La presenza non è interpretata come abbandono; non vengono validati, adottati, rimossi o ripubblicati. L’enumerazione non prende lock e costituisce un’istantanea consultiva: un publisher concorrente può cambiare lo stato durante o subito dopo l’ispezione. Un test su fixture termina con SIGKILL un processo dopo la creazione di un payload parziale, quindi verifica che l’ispezione lo elenchi e che i byte restino presenti. È recovery osservativa, non automatica.

### Store immutabile, generazioni e transazioni install/remove

Il percorso moderno realizza oggetti immutabili con preparazione privata, verifica dell’identità, barriere fsync e rename NOREPLACE; i pin di transazione coordinano GC e membership. Le generazioni sono pubblicate da directory privata con file sincronizzato, rename NOREPLACE e barriera del parent. La selezione corrente è un record separato: pubblicare una generazione non equivale a selezionarla. Gli stage privati residui da crash non hanno sweep automatico.

Installazione: preflight semantico, registrazione pin, realization, pubblicazione generazione target, switch e handoff pin. Se lo switch fallisce, il chiamante prova a scartare la generazione inattiva; l’errore originale è preservato e i pin non vengono ritirati arbitrariamente. Rimozione costruisce una generazione senza i pacchetti richiesti e usa lo stesso percorso di switch/handoff. Lo switch CLI e il rollback selezionano una generazione già pubblicata; la cronologia di selezione non è ricostruibile dal solo massimo ID e la scelta rollback storica non equivale a una transazione registrata.

La selezione/recovery moderna identifica base o target tramite record current e piano pubblicato; non promuove implicitamente un piano pendente. La reconciliation applica operazioni autorizzate e idempotenti riconoscendo stato precedente o finale, verifica la proiezione completa e conserva le evidenze. Un errore dopo progressi può lasciare stato misto e risultato `OutcomeUncertain`; non viene inventato un rollback automatico. La rimozione delle proiezioni dipende dai witness e non autorizza cancellazioni ricorsive.

**Limiti:** gli errori di processo sono coperti da fault injection e SIGKILL in fixture; ciò non simula perdita di alimentazione, cache del dispositivo, barriera ignorata, journal replay o filesystem target Debian. Anche il protocollo moderno ha fasi multi-namespace e non fornisce una singola transazione atomica fra generazione, selezione, proiezione e pin. Le decisioni recovery per residui ambigui restano fail-closed e manuali.

## Modifiche M4

- `src/store/store.hpp`, `src/store/store.cpp`: API read-only per elencare candidati stage, rifiuta una root store che non sia directory reale, non muta alcun candidato.
- `tests/store_metadata_test.cpp`: crash simulato SIGKILL su directory stage parziale; verifica classificazione e conservazione dei dati.
- Nessuna modifica a formati persistenti, pin, protocolli privileged o cleanup automatico.

## Verifiche

| Verifica | Esito |
|---|---|
| Configure Debug pulito | **PASS** — `/tmp/monad-m4-recovery-build` |
| Build completa Debug | **PASS** — tutti i target compilati |
| CTest selezionato non privilegiato | **PASS — 108/108**, 0 FAIL, 0 SKIP, 0 TIMEOUT. Esclusi prima dell’esecuzione: label `privileged`/`diagnostic`, test scanner e integration, `qualification_driver_test`, test Alpha M1 e checkpoint `activation_plan_persistence_boundaries_*`; il caso `activation_selection_test` non ha prodotto esito leggibile ed è stato interrotto. |
| Regression store/crash recovery | **PASS** — incluso nel 108/108 e rilanciato singolarmente (1/1 PASS). |
| Test mirati installazione/generazione/reconciliation | **PASS — 4/4** — `store_metadata_test`, `install_transaction_test`, `generation_test`, `activation_reconciliation_test`. |
| `activation_plan_persistence_boundaries_*` | **NOT RUN** — un lancio CTest più ampio si è interrotto senza riepilogo durante il checkpoint `boundaries_reused`; escluso dal lancio finale completato. |
| `activation_selection_test` | **NOT RUN** — il runner non ha fornito risultato affidabile; non conteggiato come skip o pass. |
| Casi privilegiati, scanner non autorizzati, collector, Sessione 68 | **NOT RUN** per vincolo esplicito. |
| Verifica reale di power-loss/reboot su VM o filesystem target | **BLOCKED** — non autorizzata in questa sessione. |
| `git diff --check` | **PASS** |

Il test M4 simula la morte di un processo, non un blackout. Build e prove sono locali su fixture temporanee; non sono state toccate installazioni reali.

## Matrice Alpha aggiornata

| Area | Stato M4 | Evidenza / blocco residuo |
|---|---|---|
| Download, estrazione, verifica e store legacy | **IMPLEMENTATO / PARZIALMENTE VERIFICATO** | Copia privata e pubblicazione NOREPLACE; recovery stage ora osservativa. Persistenza legacy senza fsync e residui ambigui non sono risolti automaticamente. |
| Pubblicazione oggetti immutabili | **IMPLEMENTATO / VERIFICATO LOCALMENTE** | Suite non privilegiata include fault/crash meccanici; scansione di produzione e qualificazione storage restano separate. |
| Install/remove e pin | **IMPLEMENTATO / PARZIALMENTE VERIFICATO** | Install/remove costruiscono generation e handoff pin; test locali passano. Residui pin conservativi e transazione multi-namespace restano. |
| Generazioni, switch, rollback e reconciliation | **IMPLEMENTATO / PARZIALMENTE VERIFICATO** | Pubblicazione NOREPLACE, selezione/recovery e reconciliation idempotente; alcuni checkpoint non privilegiati non hanno esito affidabile in M4. |
| Recovery dopo perdita di alimentazione | **BLOCKED** | Nessuna campagna di reboot/power-cut su filesystem supportato o VM Debian. |
| Chiusura input ufficiali e pin candidato | **BLOCKED** | Mismatch inventario M1/M2 non promosso; pin non aggiornati. |
| Scanner fisico, campagna privilegiata Debian ed evidence review | **BLOCKED / NOT RUN** | Nessun test privilegiato né verifica VM eseguita. |
| Qualificazione Alpha M1 | **BLOCKED** | Resta esplicitamente BLOCKED; M4 non autorizza GO. |

## Rischi residui e decisione

1. Gli stage legacy orfani possono accumularsi e richiedere review manuale; il solo nome non dimostra provenienza, completezza o sicurezza per rimozione. L’elenco è consultivo e non è sincronizzato con publisher concorrenti.
2. La pubblicazione legacy atomica non è durevole contro perdita di alimentazione perché mancano barriere esplicite.
3. L’atomicità complessiva di install/remove attraversa store, pin, generazione, current e proiezione; errori intermedi possono richiedere recovery esplicita.
4. Non sono stati qualificati filesystem reali dopo reboot né il comportamento fisico della campagna Debian.
5. Inventario ufficiale, security pin, scanner e gate privilegiati rimangono aperti come riportato in M1–M3.

**Decisione: NO-GO; Alpha M1 resta BLOCKED.**
