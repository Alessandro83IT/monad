# Monad 0.1 Alpha M7 — provenienza sorgenti, build e artefatti

**Decisione: NO-GO. Alpha M1 resta BLOCKED.** M7 implementa un’attestazione locale riproducibile e completa una build Debug non privilegiata del checkout attuale. L’attestazione lega hash locali di input, configurazione e artefatti, ma non è firmata, indipendentemente custodita o autenticata: non prova approvazione, identità del builder né causalità sorgente-oggetto.

## Perimetro e stato iniziale

- Branch `main`, `HEAD` iniziale `abd6bac4b368427b76f6c98522afde928b418939`.
- Il checkout era già ampiamente modificato e conteneva file non tracciati, build e prove storiche. Sono stati preservati; non sono stati eseguiti reset, clean, commit o push.
- Sono stati letti integralmente i rapporti M1–M6 in `docs/development/monad-0.1-alpha-m{1-package-audit,m2-stabilization,m3-security,m4-recovery,m5-integration,m6-reconciliation}-20261027.md`.
- Nessuna installazione reale, test privilegiato, collector Alpha M1, driver host Sessione 68, lettura di credenziali o modifica di baseline, inventari ufficiali, pin o fixture D6.
- La build è isolata in `/tmp/monad-m7-provenance-build`; le evidenze aggiunte sono in `docs/development/monad-0.1-alpha-m7-evidence-20261027/`.

## Requisiti Alpha M1 ricostruiti da M1–M6

| Requisito | Evidenza cumulativa | Stato dopo M7 |
|---|---|---|
| CLI, manifest, resolver, repository, download, archive, backend CMake/Autotools, store | Implementazione e regressioni locali M1–M3; strictness dello schema legacy ancora aperta | Implementato, solo build locale in M7; nessun test CTest eseguito in M7 |
| Store immutabile, generazioni, selection, install/remove, recovery | M3–M5: pubblicazione, transizioni, fault injection e crash di processo su fixture | Verifiche precedenti restano evidenza storica locale; nessuna nuova verifica funzionale M7 |
| Chiusura e review degli input | Inventario ufficiale 414 input; M2 candidate 426; M6 riproduzione 427 con una aggiunta e sei digest diversi rispetto al candidate M2 | **BLOCKED**: nessuna promozione autorizzata; attestazione M7 ha uno scope candidato distinto di 424 input |
| Identità sorgente e binari legati alla build | HEAD da solo non identifica le numerose modifiche locali; M1–M6 non avevano una catena unica autenticata | Attestazione locale implementata e verificata, ma **non autenticata** e non prova la causalità binaria |
| Pin binari e amministrativi | Pin installer e valori di confronto M6 | Hash confrontati; nessun pin cambiato o promosso |
| Scanner fisico, casi privilegiati e campagna Debian VM | Requisiti di staging/recovery e campagne M1; tutti i report mantengono NO-GO/BLOCKED | **NOT RUN / BLOCKED** |
| Decisione di rilascio | Richiede source closure approvata, pin promossi con review, prove richieste e revisione indipendente delle evidenze VM | **NO-GO** |

M1 riportava 116 PASS, 1 FAIL (`qualification_driver_test`), 7 SKIP e 0 TIMEOUT nella selezione ampia; M2 riportava 116 PASS e lo stesso FAIL. M3 completava sei test mirati PASS; M4 riportava 108/108 PASS nella propria selezione filtrata, con alcuni checkpoint NOT RUN; M5 riportava 108 PASS e 1 FAIL nella suite selezionata e PASS separati per cinque checkpoint. Sono risultati storici dei rispettivi report, non rieseguiti da M7. M6 ha riconciliato i conteggi e documentato la deriva dell’inventario e l’assenza di provenienza autenticata.

## Analisi del sistema di build e inventari

Il `CMakeLists.txt` corrente richiede CMake 3.20, progetto C++20, OpenSSL Crypto, LibArchive e Threads. Raccoglie ricorsivamente `src/*.cpp` (escluso `src/main.cpp`) per `athena_core`, costruisce il CLI `mpkg`, abilita `CMAKE_EXPORT_COMPILE_COMMANDS` e aggiunge numerosi eseguibili CTest. Include target di packaging Linux e un target `appimage`. Il build M7 ha usato il target predefinito completo e ha compilato anche eseguibili privilegiati/scanner; **nessuno di questi è stato eseguito**.

Il test driver ufficiale `tests/qualification_driver_test.py` usa `tests/bounded_toml_sources.txt` e uno scope di 414 file (sette root e sei file fissi), rifiutando symlink e omettendo `__pycache__`. M2 ha registrato 12 file non elencati; M5 ha descritto 13 non elencati dopo l’aggiunta di `tests/archive_security_test.cpp`. M6 ha riprodotto lo scope candidate corrente a 427 file: una aggiunta e sei digest differenti dal JSON M2 storico. I conteggi non sono intercambiabili: la raccolta M7 usa le root candidate `src`, `include`, `tests`, `cmake`, `third_party`, `packaging`, `packages`, il solo file fisso `CMakeLists.txt` e, per tracciare il nuovo attestatore, i suoi due file. Questa scelta produce **424 input**, esclude documenti e strumenti esterni a tali radici (salvo i due file espliciti) e non sostituisce né riconcilia l’inventario ufficiale.

Le modifiche sorgente preesistenti sono rappresentate dal `git status --porcelain` integrale e dall’hash del diff tracciato rispetto a `HEAD` nell’attestazione; gli hash per file coprono lo scope dichiarato, inclusi file non tracciati in quelle directory. Il record non acquisisce i contenuti di ogni elemento fuori scope né costituisce una source closure approvata.

## Pin esaminati

M6 documenta sette pin binari di confronto e quattro pin amministrativi. M7 non ha estratto o rigenerato pin da installer e non li ha modificati; la tabella sotto usa i valori di riferimento riportati da M6.

| Artefatto | Pin storico M6 | Hash build M7 | Confronto |
|---|---|---|---|
| `activation_execution_privileged_test` | `d87a84cbb1e18ba073beaadb1bb7737b61fa448081af37e4d4c5a6c29337d157` | `e9ddcbd05418892fae89af4a42b95b7d15674496be20660e4def6fd76f84a590` | Diverso |
| `activation_plan_retention_test` | `f76251d6e0af0bd892d1dcb17f41f80b13343c7c274e289014dfcb090370e474` | `125280b20a7c4dfe750ffe1190391c59bbaf704c3190ebf92e304efa8ff4520e` | Diverso |
| `activation_plan_persistence_test` | `f12620d89bb55e66f1e1960dd8852f2fdf27c9a148967d222d562fdddbd5aaa3` | `500c6c586b52366bb887316630484a2baf5e91207698a60fe7e87f0df48f69d7` | Diverso |
| `legacy_d3_test` | `61e23e8c9cd3a81c8766eb61e93e01cc7c9af2025a80f6056f28d3ac137ad6a9` | `bfa5cfa569a32302ae9287514f348fe04e6b64511981cb97c63df71d93887f8a` | Diverso |
| `initial_activation_test` | `934222dc181ae1a98a3d00bed5cba8e208cdad194d0c10fe552b1e5cf7e74243` | `000ca18058c62466df944c46cf79ccf6fcbe8820ddd38edf3e5d5da0f4962a7c` | Diverso |
| `libathena_core.so` | `e212420cb34aeca76cebe2ecf396832f25c3c4f119b846b42ef00d2994cc995a` | `ba80321ba09e1cc57f5c4294b412914fec50abcb8ac7c05b101861182a4307d2` | Diverso |
| `CTestTestfile.cmake` | `99751015487882864d67b948dbbb0eee4ac7204aca1b7ee20435f8e69b656bd5` | `fed72115ca1a16e620e6ed9e96cef72742901655931680eef79fcfd60029fd9d` | Diverso |

I quattro pin amministrativi risultano, secondo la verifica locale già riconciliata da M6, byte-validi rispetto ai file attuali: runner `c68bca67…`, gateway support `92c928ca…`, sudoers draft `263c1197…`, canary systemd `9d9d4d84…`. Questa corrispondenza locale non dimostra approvazione per uso, policy sudoers effettiva o identità installata sulla VM. M7 non li ha ricontrollati operativamente e non li considera autorizzati.

Gli hash M7 diversi sono una differenza osservata tra artefatti locali e riferimenti storici, non una proposta di sostituzione. Dipendono almeno dal checkout/configurazione/toolchain correnti e non sono promuovibili senza workflow di review separato.

## Procedura e strumenti M7

Aggiunti:

- `scripts/monad_build_attest.py`: comandi `create` e `verify`; registra HEAD/branch, stato Git, diff tracciato, hash e dimensioni dei file nello scope, scope/omissioni, cache CMake, compilatore/versione, dipendenze dichiarate, comandi, riferimenti e hash dei log, hash/dimensioni degli artefatti e digest canonico del record.
- `tests/monad_build_attest_test.py`: sei controlli sintetici su record valido, modifica input, sostituzione artefatto, record incoerente, symlink input e rifiuto di sovrascrittura.

Procedura eseguita:

```sh
cmake -S . -B /tmp/monad-m7-provenance-build \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build /tmp/monad-m7-provenance-build --parallel 2
python3 scripts/monad_build_attest.py create \
  --build /tmp/monad-m7-provenance-build \
  --output docs/development/monad-0.1-alpha-m7-evidence-20261027/local-build-attestation.json \
  --command 'cmake -S . -B /tmp/monad-m7-provenance-build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON' \
  --command 'cmake --build /tmp/monad-m7-provenance-build --parallel 2' \
  --log docs/development/monad-0.1-alpha-m7-evidence-20261027/configure.log \
  --log docs/development/monad-0.1-alpha-m7-evidence-20261027/build.log \
  --dependency 'GNU C++ 14.2.0' \
  --dependency 'OpenSSL Crypto 3.5.7' \
  --dependency 'LibArchive 3.7.4' \
  --dependency 'Threads (CMake)' \
  --dependency 'Python 3.13.5'
python3 scripts/monad_build_attest.py verify \
  --record docs/development/monad-0.1-alpha-m7-evidence-20261027/local-build-attestation.json
```

Il verificatore ricalcola input, artefatti, cache, log esistenti e digest record. Il digest canonico è un controllo di coerenza, non una firma: chi può riscrivere il record può ricalcolarlo. Il verificatore non ricompila e non può provare che un artefatto sia stato causato dagli input registrati. Hash e metadati sono raccolti dallo stesso host e dallo stesso workspace.

## Evidenze e risultati

Ambiente registrato: GCC 14.2.0, CMake Debug, OpenSSL Crypto 3.5.7, LibArchive 3.7.4, Threads e Python 3.13.5. La configurazione CMake ha avuto **PASS**; build predefinita completa **PASS** (100%). Sono stati inventariati 101 artefatti. La verifica immediata dell’attestazione ha avuto **PASS**. La suite sintetica Python ha avuto **PASS 6/6**.

| Controllo M7 | Stato | Evidenza |
|---|---|---|
| Configurazione CMake isolata | **PASS** | `evidence/configure.log` |
| Build Debug completa, non privilegiata | **PASS** | `evidence/build.log`; nessun eseguibile lanciato |
| Verifica record/input/cache/log/artefatti | **PASS** | `evidence/local-build-attestation.json` |
| Test sintetici attestatore | **PASS — 6/6** | `python3 tests/monad_build_attest_test.py` |
| CTest funzionali del prodotto | **NOT RUN** | M7 compila i test, ma non li esegue |
| Test privilegiati, scanner, installazione e Debian VM | **NOT RUN** | Esclusi per vincolo |
| Confronto sette pin binari | **Diversi tutti e sette** | Tabella sopra; nessun pin aggiornato |
| Pin amministrativi | **Match locale già riportato da M6; approvazione non stabilita** | Nessuna installazione/ispezione VM |
| Source closure ufficiale e promozione | **BLOCKED** | Nessun inventario ufficiale modificato |

Hash SHA-256 degli artefatti di evidenza:

- Record attestazione: `e1e3f3a8abec495c740377e2f2f51c929874ae28075bec0ff1bdc73250bd98b2`
- Log configure: `30fe09668dc2ead2e64b743a08cf55bc71f46b05089b3607e5ff6880727efe27`
- Log build: `1a690569d37699de883224b6c013c5da78af1c48477cb9ef2f2060216d95f367`
- Manifest input nell’attestazione: `c2c9f1f1e98ffccdb7c6d60f77dcb2a791663b073a7d9c7414ba6487a153e32e`
- Digest record interno: `461595c9fd68533b1f0dd08378479ec6ed6638c194e6c3b9b8f4d4a409a97aa1`

Per riverificare gli hash esterni: `sha256sum docs/development/monad-0.1-alpha-m7-evidence-20261027/{local-build-attestation.json,configure.log,build.log}`. Per riverificare la coerenza locale: eseguire il comando `verify` sopra nello stesso checkout e mantenere disponibile `/tmp/monad-m7-provenance-build`.

## Limiti e rischi residui

1. **Nessuna autenticazione indipendente.** Nessuna firma, attestatore hardware, identità verificata del builder, transparency log, custodia separata o riproduzione indipendente. Hash locali possono rilevare drift solo rispetto al record disponibile.
2. **Input non approvati.** I 424 input definiscono uno scope tecnico M7, non la source closure ufficiale. File esterni alle root/fissi, toolchain completa, ambiente di processo, generatori, timestamp e dipendenze indirette possono essere incompleti.
3. **Toolchain e dipendenze.** Versioni registrate non sono lockfile immutabili; hash/compiler package, linker, sysroot, libc e tutte le dipendenze transitive non sono state sigillate o acquisite in un ambiente riproducibile.
4. **Causalità e riproducibilità.** L’attestatore compara file finali e input; non registra hash di ogni object file con una provenienza verificabile, non effettua rebuild indipendente e non garantisce determinismo bit-a-bit.
5. **Integrità e custodia.** Il record è riscrivibile e il digest non è autenticato. Log e record nella stessa checkout non proteggono da un amministratore o processo che alteri entrambi.
6. **Build non sandboxata.** La compilazione è stata invocata come utente normale, ma non in sandbox contro script/plugin/tool malevoli. M1–M6 documentano inoltre che i build script pacchetto mantengono l’autorità dell’utente.
7. **Qualificazione aperta.** Restano mismatch inventario, schema legacy, rischi di recovery/power-loss, prova scanner fisica, pin promotion, trasferimento/deployment e campagna VM privilegiata con evidence review indipendente.

## Requisiti per una futura qualificazione VM

Prima di usare questa procedura per un candidato qualificabile occorrono: freeze e review del source tree e dello scope input; promozione esplicita dell’inventario in un artefatto versionato distinto; toolchain/dependency lock e ambiente Debian approvato; build isolata da checkout immutabile con comandi e log completi; attestazione firmata da identità autorizzata o raccolta in un sistema di custodia indipendente; rebuild indipendente e confronto artefatti; review security separata di tutti i pin e del manifest di trasferimento; prova VM di hash, proprietà, modalità e ACL degli artefatti trasferiti; esecuzione della campagna esatta approvata, inclusi tutti i casi privilegiati e i requisiti scanner/filesystem; conservazione di D6, log, ricevute, report e backup; infine revisione indipendente di ogni evidenza e gestione esplicita di ogni FAIL, SKIP, TIMEOUT o NOT RUN. La procedura M7 da sola non autorizza staging né installazione.

## Decisione

**GO/NO-GO: NO-GO. Alpha M1 resta BLOCKED.** La build locale è riuscita e il nuovo record rileva modifiche successive agli input e agli artefatti. Ciò fornisce una catena di hash locale utile alla diagnosi; non stabilisce provenienza autenticata, promozione pin, source closure approvata o qualificazione Debian VM. Nessun artefatto M7 è sostitutivo automatico di un pin ufficiale.
