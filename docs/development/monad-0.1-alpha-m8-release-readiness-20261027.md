# Monad Package Manager 0.1 Alpha M8 — release readiness

**Decisione: NO-GO. Alpha M1 resta BLOCKED.** Questa sessione prepara un candidato locale non installabile automaticamente e una proposta di source closure. Non dichiara la release pubblicata, qualificata o approvata.

## Sintesi M1–M8

I rapporti M1–M7 sono stati letti integralmente. M1 documenta build Debug PASS e suite selezionata 116 PASS, 1 FAIL (`qualification_driver_test`), 7 SKIP; M2 conferma il problema dell’inventario ufficiale e registra 31 test esclusi; M3 riporta sei test mirati PASS e correzioni di sicurezza su archivio/store; M4 riporta 108/108 PASS nella sua selezione filtrata, con checkpoint e casi non eseguiti esplicitamente; M5 registra 108 PASS, 1 FAIL nel run selezionato e cinque checkpoint separati PASS; M6 riconcilia divergenza inventari, pin e provenienza; M7 completa build Debug locale e attestazione hash, senza autenticazione. Questi risultati sono storici e non sono stati sommati né ripetuti da M8.

Stato requisiti Alpha M1: funzionalità implementate con copertura locale parziale; schema legacy e recovery restano limitati; inventory/source closure ufficiale, pin, provenienza autenticata, prova fisica dello scanner, casi privilegiati e campagna Debian VM sono aperti. Nessun risultato locale chiude i gate di qualificazione.

## Candidato locale

Identificatore proposto: `0.1.0-alpha.1+20261027.local1`. È un identificatore di lavoro, non una versione pubblicata. Il candidato comprende i 424 input della closure JSON indicata sotto, modifiche correnti tracciate e file non tracciati dentro le radici, insieme alle dipendenze dichiarate dal progetto. L’output M7 era una build Debug locale; non è l’artefatto approvato di questo candidato e non viene distribuito. Nessun installer automatico è incluso in questa proposta.

Requisiti funzionali d’accettazione: build pulita da closure congelata; casi non privilegiati dell’inventario approvato PASS; CLI, parsing/validazione, risoluzione, repository, acquisizione/verifica, store, generazioni, install/remove/rollback/recovery superano casi assegnati; nessun FAIL, SKIP, TIMEOUT o NOT RUN nei gate obbligatori senza deroga approvata.

Requisiti di sicurezza: hash verificati su input e trasferimento; build e toolchain identificati; revisione della closure e dipendenze; test scanner e filesystem sul target; pin proposti attraverso review separata; privilegi confinati al gateway approvato e agli argomenti allowlist; evidenze integre e custodia verificabile.

Limitazioni note: inventario ufficiale 414 non aggiornato; M2 candidate 426, M6 427, M7 closure tecnica 424 hanno scope diversi. Build non sandboxata; toolchain/dependency lock non sigillati; attestazione locale non firmata e non prova causalità sorgente-binario; strictness dello schema legacy, stage ambigui e atomicità multi-namespace/recovery restano aperti.

Condizioni che impediscono il rilascio: qualsiasi divergenza tra closure approvata, build, trasferimento e VM; mismatch o pin non revisionati; test obbligatorio mancante/FAIL/SKIP/TIMEOUT/NOT RUN; preflight incompleto; perdita/modifica di fixture D6 o prove; assenza di approvazione amministrativa e revisione indipendente; qualunque claim di autenticità fondato solo su hash locali.

## Source closure proposta

Manifest versionato: [`monad-0.1-alpha-m8-source-closure-20261027.json`](monad-0.1-alpha-m8-source-closure-20261027.json), SHA-256 `8ff98f55fafd85808228a97e878e44b9284325364e774bfaac8d1005b8a00d4c`. Il manifest contiene 424 percorsi con SHA-256 e dimensione, radici, input fissi, baseline HEAD/diff, 124 record di stato Git e classi di esclusione. Il digest si verifica con `sha256sum docs/development/monad-0.1-alpha-m8-source-closure-20261027.json`. Gli hash per input derivano dallo scope dell’attestazione M7; quest’ultima è stata riverificata durante M8. La chiusura è riproducibile/verificabile localmente, non promossa.

Baseline: branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`. Il diff tracciato include 23 file e 639 inserimenti / 249 rimozioni secondo `git diff --stat` all’avvio M8. Fra questi: CMake, archive/download/build, package/repository/store, fixture e test. Lo stato iniziale includeva inoltre 124 voci Git modificate/non tracciate; i nomi e status completi sono nel manifest. Non sono state attribuite a M8 le modifiche già presenti. In particolare build directory e prove/documenti già presenti restano lavoro preesistente.

Radici tecniche: `src`, `include`, `tests`, `cmake`, `third_party`, `packaging`, `packages`; file fissi: `CMakeLists.txt`, `scripts/monad_build_attest.py`, `tests/monad_build_attest_test.py`. Dipendenze dichiarate da CMake: CMake ≥3.20, C++20, OpenSSL Crypto, LibArchive, Threads; Python 3 è usato da tooling/test. Il record M7 identifica GCC 14.2.0, OpenSSL 3.5.7, LibArchive 3.7.4, Python 3.13.5. Queste versioni non sono lock né attestazione di provenienza.

Input di compilazione: 424 file elencati; configurazione e ambiente CMake/toolchain; dipendenze di sistema; comandi effettivi registrati nell’attestazione M7. Input di qualificazione: closure congelata, immagine Debian approvata, inventario privilegiato approvato, file di configurazione/cache e toolchain identificati, verifiche di trasferimento, fixture D6 preservata e ricevute/log della campagna. L’attestazione M7 non acquisisce tutti gli input esterni a scope.

Esclusi dal bundle tecnico: output build e cache `__pycache__`/`.pyc` come generati; documenti/evidenze e strumenti di qualificazione/amministrazione fuori radici (incluse `docs/development`, `docs/qualification`, `docs/security`, `qualification`, `scripts` tranne l’attestatore esplicito, `tools`, configurazione `.codex*`, `AGENTS.md`). L’esclusione tecnica non significa irrilevanza: runbook, pin, baseline, collector e prove sono input/procedure di qualifica da identificare separatamente. Nessun inventario ufficiale è stato modificato.

## Pin e provenienza

Tabella M7 confrontata con i riferimenti storici M6; i sette hash locali differiscono tutti. Sono hash locali osservati, non valori proposti per promozione.

| Artefatto | Pin storico | Hash build locale M7 | Esito |
|---|---|---|---|
| `activation_execution_privileged_test` | `d87a84cbb1e18ba073beaadb1bb7737b61fa448081af37e4d4c5a6c29337d157` | `e9ddcbd05418892fae89af4a42b95b7d15674496be20660e4def6fd76f84a590` | diverso |
| `activation_plan_retention_test` | `f76251d6e0af0bd892d1dcb17f41f80b13343c7c274e289014dfcb090370e474` | `125280b20a7c4dfe750ffe1190391c59bbaf704c3190ebf92e304efa8ff4520e` | diverso |
| `activation_plan_persistence_test` | `f12620d89bb55e66f1e1960dd8852f2fdf27c9a148967d222d562fdddbd5aaa3` | `500c6c586b52366bb887316630484a2baf5e91207698a60fe7e87f0df48f69d7` | diverso |
| `legacy_d3_test` | `61e23e8c9cd3a81c8766eb61e93e01cc7c9af2025a80f6056f28d3ac137ad6a9` | `bfa5cfa569a32302ae9287514f348fe04e6b64511981cb97c63df71d93887f8a` | diverso |
| `initial_activation_test` | `934222dc181ae1a98a3d00bed5cba8e208cdad194d0c10fe552b1e5cf7e74243` | `000ca18058c62466df944c46cf79ccf6fcbe8820ddd38edf3e5d5da0f4962a7c` | diverso |
| `libathena_core.so` | `e212420cb34aeca76cebe2ecf396832f25c3c4f119b846b42ef00d2994cc995a` | `ba80321ba09e1cc57f5c4294b412914fec50abcb8ac7c05b101861182a4307d2` | diverso |
| `CTestTestfile.cmake` | `99751015487882864d67b948dbbb0eee4ac7204aca1b7ee20435f8e69b656bd5` | `fed72115ca1a16e620e6ed9e96cef72742901655931680eef79fcfd60029fd9d` | diverso |

Proposta separata: dopo freeze, rebuild su immagine approvata e revisione security indipendente, generare un documento proposto con artefatto, path, hash atteso/osservato, fonte, builder, toolchain, approvatore e motivazione; confronto indipendente e approvazione esplicita precedono qualsiasi patch ai pin. In M8 i pin ufficiali sono invariati.

Distinzioni: **hash locali** = digest ricalcolabili nello stesso workspace; **provenienza ricostruita** = legame documentale tra HEAD/diff/input/config/log/artefatti; **provenienza autenticata** = non disponibile; **approvazione indipendente** = non avvenuta. Attestazione M7 e verify M8 non firmano, non provano causalità e non autenticano builder o approvazione.

## Verifiche M8

| Verifica | Esito |
|---|---|
| `git diff --check` | PASS |
| Verifica attestazione M7 input/cache/log/artefatti | PASS, coerenza locale soltanto |
| Test sintetici `tests/monad_build_attest_test.py` | PASS 6/6 |
| Inventario CTest via `ctest --show-only=json-v1` | PASS, 149 registrati; nessun test eseguito |
| Build M8 | NOT RUN; il build M7 storico è PASS, non è build della closure M8 congelata |
| CTest funzionali | NOT RUN |
| Privileged/scanner/diagnostic, collector, installazioni, Sessione 68 | NOT RUN per vincolo esplicito |
| Debian VM e controlli fisici/provenienza | NOT RUN |
| FAIL / SKIP / TIMEOUT in verifiche eseguite M8 | 0 / 0 / 0 |

Comandi di verifica M8: `python3 scripts/monad_build_attest.py verify --record docs/development/monad-0.1-alpha-m7-evidence-20261027/local-build-attestation.json`; `python3 tests/monad_build_attest_test.py`; `git diff --check`; `ctest --test-dir /tmp/monad-m7-provenance-build --show-only=json-v1` (solo discovery). Non è stato eseguito CTest.

## Qualification readiness Debian VM

### Preflight GO/NO-GO

1. Congelare il commit e closure approvati; confrontare ogni SHA-256, symlink e dimensione. Qualsiasi differenza: NO-GO.
2. Verificare immagine Debian, snapshot, boot ID, filesystem/mount, account, spazio e orologio; verificare integrità e provenienza dell’immagine da fonte approvata.
3. Ricostruire in ambiente isolato con toolchain e dipendenze approvate; confrontare attestazione/build indipendente e pin proposti. Provenienza non verificabile: NO-GO.
4. Controllare solo in lettura inventario CTest, selezione esatta, etichette `privileged`, scanner/diagnostic e hash runner/gateway/canary/sudoers; nessuna discrepanza o pin non approvato.
5. Verificare che fixture Alpha M1 D6, log, ricevute, report, backup e residui siano presenti e invariati; registrare hash e owner/mode/ACL senza rimuovere né riparare automaticamente.
6. Indipendente revisore esamina manifest, comandi, risultati, differenze, pin e piano di rollback. Tutte le approvazioni richieste devono esistere prima di azioni riservate.

### Classi operative

- **Non privilegiate:** confronto hash, analisi statica, `ctest --show-only=json-v1`, inventario toolchain, build isolata e test sintetici non privilegiati. Nessuna di queste prove autorizza installazione.
- **CAP_SYS_ADMIN:** scanner che dimostra assenza di `trusted.*` xattr nell’initial namespace e casi filesystem/protocollo che dichiarano tale requisito. Devono essere lanciati solo come casi esatti nella VM approvata e nella campagna autorizzata; niente tentativi degradati.
- **Autorizzazione esplicita:** staging/installazione gateway, sudoers o policy, canary/receipt, installazioni reali, qualsiasi modifica amministrativa o privilegio, rollback state-changing. Sono fuori M8.
- **Collector Alpha M1:** raccoglie evidenza privilegiata; non eseguire finché candidato, host e procedura non siano approvati. M8 non lo ha lanciato.
- **Installazioni reali:** nessuna; richiedono gate separato dopo i test e approvazione esplicita.
- **Provenienza/integrità:** verificare identità e hash di immagine, sorgenti, binari, trasferimento, configurazione e prove. Hash senza custodia/firma autenticata non basta.

### Campagna approvata

Dopo i gate e le autorizzazioni: identificare snapshot/boot ID; eseguire preflight read-only; confrontare inventory privilegiato esatta col piano; eseguire solo i casi elencati dal runbook approvato; mantenere log grezzi e exit status; nessun retry che sovrascriva prove; preservare D6, report, ricevute e backup; eseguire controlli post-run e riconciliazione. PASS solo se ogni caso obbligatorio è PASS e l’evidence review indipendente chiude ogni discrepanza. FAIL, SKIP, TIMEOUT, NOT RUN o prova mancante implica NO-GO/BLOCKED. M8 non modifica il runbook ufficiale e non esegue alcuna fase riservata.

## Rischi e decisioni aperte

- Review umana della closure tecnica e decisione su come riconciliare inventari 414/426/427/424 senza alterare prove storiche.
- Review della strictness del manifest legacy e dei limiti di recovery/durabilità.
- Definizione e acquisizione riproducibile di toolchain/dependency set; build trust e sandbox.
- Revisione security indipendente e approvazione separata dei sette pin candidati e degli eventuali pin amministrativi.
- Approvazione VM/campagna e successiva evidence review indipendente.
- Nessuna attestazione M7/M8 costituisce revisione indipendente.

## Piano concreto M9

1. Sottoporre manifest M8 e lista 124 status a review indipendente; produrre una closure congelata approvata distinta da tutti gli inventari storici.
2. Risolvere i mismatch di inventario con un nuovo artefatto versionato; lasciare invariati baseline e rapporti storici.
3. Congelare build inputs Debian e ripetere build isolata da sorgente congelata; acquisire comandi, dipendenze, log, hash e confronto indipendente.
4. Risolvere e verificare le regressioni funzionali aperte (schema legacy, recovery e test non privilegiati obbligatori) senza falsificare baseline.
5. Preparare una proposta pin separata con review security e approvazione, senza aggiornare i pin ufficiali prima del workflow autorizzato.
6. Solo dopo le approvazioni richieste, eseguire la campagna VM esatta con preservazione D6/evidenze e review indipendente; ogni esito non PASS mantiene BLOCKED.
7. Emettere una decisione release separata basata su evidenze complete. Fino a quel momento: NO-GO.

**Esito finale M8: NO-GO; Alpha M1 BLOCKED.** Nessuna installazione, test privilegiato, collector, driver Sessione 68, promozione pin, modifica baseline, commit o push è stato effettuato.
