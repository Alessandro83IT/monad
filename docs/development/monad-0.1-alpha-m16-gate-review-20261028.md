# Monad 0.1 Alpha M16 — qualification gate review

**Verdetto: NO-GO alla campagna Debian VM. Alpha M1 resta BLOCKED.** La closure M15 e le evidenze locali selezionate superano i controlli di coerenza byte per byte, ma la provenienza autenticata, l'inventario ufficiale, la custodia D6, l'identità/readiness della VM e le autorizzazioni della campagna non sono stabiliti. Il presente rapporto non autorizza né avvia la campagna.

## Ambito, baseline e metodo

Verifica non distruttiva del dossier M15 e dei rapporti M7, M8, M13 e M14, oltre ai documenti D6 e runbook Alpha M1 effettivamente presenti. Nessun collector, campagna, test privilegiato, installazione, driver host Sessione 68 o lettura di credenziali è stato eseguito. Nessuna baseline, inventario o pin ufficiale è stato modificato. Non sono stati creati fixture o build temporanei.

Baseline osservata: branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`. Il worktree era già dirty all'inizio: modifiche tracciate in `CMakeLists.txt`, `src/`, `tests/` e numerosi file non tracciati, report, script, fixture e build directory. Tutto è stato preservato. Il contenuto dei 426 input tecnici, 49 input di processo e 16 documenti di contesto coincide con la closure M15; ciò attesta i byte correnti rispetto al manifest locale, non quando o da chi siano stati prodotti. Perciò l'asserzione “nessuna modifica tecnica dopo M15” è **coerente per i 491 input dichiarati**, ma non è una prova temporale/autenticata di assenza di modifiche fuori scope.

## Tabella dei gate

| Gate | Stato | Evidenza e motivazione |
|---|---|---|
| Digest closure candidata M15 | **PASS** | SHA-256 del JSON: `1864c42a6ae7871015f45c4058d7df9bd23040d59dfd475a3be5cd86f18a5be6`, pari al digest richiesto. |
| Integrità dei 426 input tecnici | **PASS** | 426/426 file presenti; dimensioni e SHA-256 corrispondono agli entry M15. |
| Integrità dei 49 input processo/qualificazione | **PASS** | 49/49 file presenti; dimensioni e SHA-256 corrispondono agli entry M15. |
| Integrità dei 16 documenti di contesto | **PASS** | 16/16 file presenti; dimensioni e SHA-256 corrispondono agli entry M15. |
| Assenza di modifiche della closure M15 | **PASS, limitato allo scope** | I file elencati sono byte-identici al manifest M15. Worktree dirty e numerosi input fuori scope; nessuna prova firmata o timestamp indipendente. |
| Inventario ufficiale di qualificazione | **FAIL / BLOCKED** | M14 registra 16 input presenti ma assenti dall'inventario ufficiale e il qualification driver fallisce in setup su input non elencati. Nessuna promozione ufficiale è avvenuta. Proposta dettagliata sotto. |
| Provenienza autenticata sorgente-binario | **BLOCKED** | Attestazione M7, manifest M8/M13/M15 e hash locali non sono firmati né custoditi indipendentemente e non dimostrano causalità sorgente-binario. |
| Sette pin binari storici | **BLOCKED** | Tutti i sette artefatti M14 locali divergono dai pin; mancano gli originali o record di build autenticati e revisione di promozione. |
| Evidenze conservate M14 dichiarate in M15 | **PASS, consistenza locale** | Nove file disponibili; hash ricalcolati tutti corrispondenti ai valori del dossier M15. Root M14 e build sono mode `0700`. Nessuna prova di custodia indipendente. |
| Residuo storico D6 Alpha M1 | **NOT VERIFIED** | Runbook identifica `/var/tmp/athena-d67-ext4/athena-d6-8yRDjc`, con `generations`, `projection`, `store` e `generations/.athena-locks/state.lock` da preservare. Rapporti storici dicono che l'attraversamento era negato. Non si è tentato accesso privilegiato. Le directory `athena-d6-*` M13/M14 sono fixture dei test locali, non prova del residuo originale. |
| Completezza/tracciabilità dell'evidence storica | **NOT VERIFIED** | M14 conserva log, inventory e TSV descritti sotto. La documentazione storica indica prove protette/non traversabili e controlli ancora necessari su ricevute, backup, fixture, ACL e D6. Non è ricostruibile una catena di custodia autenticata dai soli file locali. |
| VirtualBox e configurazione VM | **BLOCKED** | `VBoxManage`, `VirtualBox` e `VBoxHeadless` non sono disponibili nel PATH dell'ambiente corrente; nessuna VM o snapshot è stata interrogata/avviata. Identità Debian, snapshot, risorse guest e isolamento restano non verificati. |
| Readiness operativa Debian VM | **BLOCKED** | Nessuna prova corrente di VM approvata, snapshot, boot ID, capacità, filesystem, toolchain e gateway root-owned pin-hash verificato. Le procedure sono documentate, ma non dimostrano stato operativo. |
| Autorizzazione campagna | **BLOCKED** | Nessuna autorizzazione esplicita per staging, invocazione del gateway, casi privilegiati, collector o rollback. Il mandato vieta tali attività. |
| Autorizzazione rilascio/installazione | **BLOCKED** | Separata dalla campagna; nessuna approvazione di release, installazione, rollback o cambiamento amministrativo. |

## D6 e prove conservate

I requisiti documentali ricostruiti da `alpha-m1-session-34-admin-runbook.md`, `alpha-m1-session-37-vm-procedure.md`, `alpha-m1-recovery.md`, `alpha-m1-vm-preflight.md` e rapporti M36–M39 sono: preservare il residuo storico senza leggere payload o mutarlo; identificare in modo no-follow tipo, owner, mode, ACL, mount/filesystem e nomi figli; mantenere riconoscibili `generations`, `projection`, `store` e `generations/.athena-locks/state.lock` ove presenti; confrontare fixture, D6, log, ricevute, backup e report; correlare hash e metadati con VM, snapshot, boot ID e run; fermarsi su accesso negato, anomalia o prova incompleta. La procedura di Sessione 38 specifica che la lettura del payload D6 è esclusa; la raccolta prevista è metadata-only e richiede approvazione esplicita del set di letture.

| Evidenza | Stato | Riscontro |
|---|---|---|
| Log M14 configure/build/test | **PASS** integrità locale | SHA-256 verificati per `final-configure.log`, `final-build.log`, `targeted-arch-process-final.log`, `activation-selection-stat-shards-final.log`, `persistence-boundary-shards-final.log`, `final-nonprivileged-suite.log`. |
| Inventory e riconciliazione 16 input | **PASS** integrità locale | `ctest-inventory-final.json` e `source-reconciliation-16.tsv` hanno hash atteso. Il TSV concorda con i file locali, ma l'inventario ufficiale rimane incompleto. |
| Qualification driver | **PASS** integrità del log; **FAIL** esito storico | `qualification-driver-final.log` ha hash atteso; registra sette subtest falliti in setup su input non elencati. |
| Residuo D6 originale e relativa prova VM | **NOT VERIFIED** | Nessun inventario metadata-only corrente accettabile per il residuo originale. I report M36/M37 riportano diniego di accesso e richiedono acquisizione separatamente autorizzata. |
| Ricevute, backup, fixture amministrativa, ACL e catena di custodia | **NOT VERIFIED** | Riferimenti storici e procedure sono presenti, ma non costituiscono riconciliazione corrente end-to-end dell'ambiente Debian approvato. |

Il controllo M16 non ricrea né sostituisce prove storiche mancanti. Le directory temporanee M13/M14 osservate restano preservate. Tutti i nove artefatti elencati nella tabella M15 sono stati ricalcolati e corrispondono: `f28c2b0c…cf17`, `b780d182…0096`, `7f487f24…bcd6`, `299d5f18…9d62`, `d3308551…3283`, `5631dc45…52da`, `629b7669…13f4`, `6d7afb4f…c246`, `13359018…1f52` (hash abbreviati qui; valori completi in M15). L'hash prova integrità byte locale, non origine, completezza semantica o indipendenza.

## Proposta di riconciliazione dei 16 input mancanti

Tutti i path sono presenti nella closure M15 e dimensioni/hash locali corrispondono. La seguente è una proposta per review dell'owner; non modifica l'inventario ufficiale e non equivale a promozione.

| Path | Classe proposta | Byte | SHA-256 |
|---|---|---:|---|
| `src/process/run.cpp` | implementazione | 1453 | `169244d601e2322792cb990ad398d005031d209ca5830e9987e46058b8a60647` |
| `src/process/run.hpp` | implementazione | 477 | `978410e1bbfb64425d2e62840a2416fb814227b1d7fec8a8b0f4f4f3d8dd3d3e` |
| `tests/alpha_m1_collector_v2_test.py` | test tooling qualificazione | 46766 | `d64a0812e88a156ef1a63f16f840498e4a49280d952d9d8febe2fdf698734451` |
| `tests/alpha_m1_collector_v2_trust_test.py` | test tooling qualificazione | 18086 | `63351fe66bb83eadfb0812a1f2edffcf081355c8820972f182f9f36a12b00881` |
| `tests/alpha_m1_ctest_inventory_test.py` | test tooling qualificazione | 13282 | `b55ac0949939811c26bbd6893a83ed7c8fb612d3ecb7c5a282415e29749865d3` |
| `tests/alpha_m1_evidence_tree_digest_test.py` | test tooling qualificazione | 4829 | `f11c9631632ee59773aea0bb432bf689ea5013ea7afa644edca41e507612d76c` |
| `tests/alpha_m1_installer_transaction_test.py` | test tooling qualificazione | 17122 | `8c00ec74ae51be70eec1c07bad3357c46e1908c39defee117f73def7a00b37da` |
| `tests/alpha_m1_receipt_path_test.py` | test tooling qualificazione | 3313 | `460d70dd4ba1b19b22bccd28dc55b96e21afb1ff66fb83ec50c82b0a014191e3` |
| `tests/alpha_m1_verified_evidence_runner_test.py` | test tooling qualificazione | 16888 | `e0366da84a01c10dfa4ce856b9fe340a1212c423e7caf5c5e3a19152a95b2994` |
| `tests/alpha_m1_verified_execution_bootstrap_test.py` | test tooling qualificazione | 14276 | `bd7c7ce361c2621940754f42900dd13b846e30afeb88ac0c8279918e81f9f699` |
| `tests/alpha_m1_vm_evidence_collector_test.py` | test tooling qualificazione | 29587 | `fc511df7cdefb17378faa792b091a0dc3750c5c138f5b52099785a766734969c` |
| `tests/arch_repository_security_test.cpp` | regressione C++ | 10892 | `d8476b3c263de683f361ef15a254b0e89331435257867b5b782a5e7ce89c1615` |
| `tests/archive_security_test.cpp` | regressione C++ | 3856 | `fdd2e373f79ea723e9db596d074a9e9ac300a21623db7b5e3e5af9d991617174` |
| `tests/monad_build_attest_test.py` | test tooling | 3305 | `d772828940682c9928f982659383ad1b2ad2bda0515e518ac6b774a5c2268ee6` |
| `tests/process_invocation_test.cpp` | regressione C++ | 2739 | `dc163ed2d941d23473664e90e118cfe1ab7cf2b59821e745722d9c068f8fb65e` |
| `tests/test_tmp_root.hpp` | supporto fixture test condiviso | 924 | `614a124e5fbb8383172f0479e94c5f5113644e26a8360fe3b6695b3aa14e4603` |

Totali: 2 implementazioni, 10 test Python tooling, 3 regressioni C++, 1 helper condiviso. Azione proposta: review del proprietario dello scope; verificare dipendenze/inclusione effettiva nei target e classificazione; poi eventuale promozione in change separata, revisionata e approvata, con nuovo esito del qualification driver. M16 non applica tale modifica.

## Pin binari divergenti

Gli hash locali sono quelli M14, ricalcolati e riportati nel dossier M15; non sono sostituti dei pin.

| Artefatto | Hash atteso storico | Hash locale M14 | Provenienza disponibile/mancante |
|---|---|---|---|
| `activation_execution_privileged_test` | `d87a84cbb1e18ba073beaadb1bb7737b61fa448081af37e4d4c5a6c29337d157` | `4be7d8265e51b28f92c363fbd64938610d54eaf73cdcb02d71f0132a73c3677e` | Hash osservato localmente; originale/build record, source/toolchain congelati, custody/firma e spiegazione del delta mancanti. |
| `activation_plan_retention_test` | `f76251d6e0af0bd892d1dcb17f41f80b13343c7c274e289014dfcb090370e474` | `f1d4292313b56cc31db75b1df6a92925c68b90a48c173d01d2027883e26424e1` | Mancano originale o record autenticato, configurazione CMake esatta e catena di custody. |
| `activation_plan_persistence_test` | `f12620d89bb55e66f1e1960dd8852f2fdf27c9a148967d222d562fdddbd5aaa3` | `01c68b15a753f43e20f4fb10ed44f0fa24208c107398db7da6e0bcf5bb3bde00` | Mancano originale/record autenticato, input sorgenti/configurazione storici e revisione. |
| `legacy_d3_test` | `61e23e8c9cd3a81c8766eb61e93e01cc7c9af2025a80f6056f28d3ac137ad6a9` | `ef41350d7b33d7d2af2406c4609ef28052ea8860fa226787548d0dbae9b86840` | Hash locale noto; provenienza binaria autenticata e spiegazione del delta mancanti. |
| `initial_activation_test` | `934222dc181ae1a98a3d00bed5cba8e208cdad194d0c10fe552b1e5cf7e74243` | `a898b8d783fcbb0f744020af16a2dc68e761af0c5434ec186b63ae00624430c8` | Mancano originale/record build autenticato, sorgenti librerie esatte e custody. |
| `libathena_core.so` | `e212420cb34aeca76cebe2ecf396832f25c3c4f119b846b42ef00d2994cc995a` | `1696d161896ffcfbf8d77863e819c42c4da25086cbfbb398bae04f99cf7479f0` | Mancano closure esatta, compiler/linker/sysroot/dipendenze/build path, attestazione autenticata e custody. |
| `CTestTestfile.cmake` | `99751015487882864d67b948dbbb0eee4ac7204aca1b7ee20435f8e69b656bd5` | `a1afa7120bbea849eebc352c19dae7b99592fd695d2fb27e5b00c524b5b8090c` | Mancano versione/opzioni CMake, path sorgente/build, inventory generato congelato e custody autenticata. |

Stato complessivo: **7/7 UNRECONCILED, NOT PROMOTED**. M9 osserva che build path diversi possono influire sui metadati incorporati: ipotesi plausibile, non riconciliazione sufficiente. Non è disponibile prova di provenienza autenticata per nessuno dei sette.

## Readiness VM e sequenza proposta

La presenza/configurazione della VM si è verificata solo passivamente per il tool host: i binari VirtualBox non sono disponibili. Non è stato possibile interrogare VM, snapshot, vCPU/RAM, spazio guest, filesystem, rete/isolamento o snapshot chain senza accesso all'hypervisor; tali elementi restano **BLOCKED**, non assunti come assenti. Il dossier M14 mostra directory evidenza locali `0700`; non dimostra isolamento Debian della VM. Le procedure in `alpha-m1-vm-preflight.md`, sessioni 34/37/39 e recovery descrivono i controlli, ma non ne attestano l'esecuzione corrente.

Sequenza esatta da applicare solo dopo gate e approvazioni:

1. **Preflight non privilegiato/read-only:** identificare VM, immagine, snapshot approvato e boot ID tramite interfaccia autorizzata; registrare Debian/kernel, clock, toolchain, filesystem/mount, risorse e capacità; verificare closure trasferita, inventory selezionato e assenza di discrepanze. Acquisire log in directory nuova `0700` sotto `/var/tmp/monad-alpha-tests`, mai sopra prove esistenti.
2. **Snapshot:** confermare snapshot disposable identificato e recovery disponibile prima di qualsiasi azione che possa modificare guest; acquisire riferimento/identificativo e revisione umana. Non avviare o ripristinare snapshot da questa sessione.
3. **Verifiche privilegiate:** solo con approvazione separata e precisa per VM, snapshot, hash/artifact root-owned, gateway, comandi/argomenti, allowlist e casi; lanciare solo casi nominati nel piano approvato. Mai eseguire binari del workspace come root o un selettore broad non revisionato.
4. **Collector:** solo se autorizzato separatamente con elenco esatto delle letture, percorsi e destinazione nuova; il collector legge dati sensibili di stato anche se non legge credenziali. Nessun collector Alpha M1 è autorizzato da questo rapporto.
5. **Validazione evidenze:** conservare stdout/stderr grezzi, exit status, timestamp, identità VM/run, hash input/output e metadati; confrontare fixture/D6/receipt/backup prima-dopo; validare completezza e catena di custody; revisione indipendente che registri PASS/FAIL/BLOCKED/NOT VERIFIED separatamente.
6. **Decisione:** GO alla fase successiva solo dopo tutti i casi obbligatori PASS, nessun elemento mancante o mismatch, prove accettate dal revisore indipendente e approvazione umana della fase successiva. Il gate di release/installazione è separato.

### Arresto e rollback

Arrestare immediatamente al mismatch VM/snapshot/hash/inventory, path o owner/mode/ACL/mount inatteso, prova D6/evidence mancante o cambiata, spazio insufficiente, processo/servizio/cgroup inatteso, timeout, FAIL, SKIP, NOT RUN di un caso obbligatorio, output incompleto o perdita di tracciabilità. Non riprovare sopra log esistenti e non pulire. Preservare stato parziale e log. Rollback è una decisione state-changing separata: solo un amministratore autorizzato può approvare il piano e l'esatto artifact/argomento; mai rollback automatico o cancellazione D6/backup/receipt. Dopo rollback verificare hash e metadata ripristinati e richiedere nuova review; rollback completato non equivale a qualifica.

### Autorizzazioni necessarie

- Approvazione del candidato immutabile, inventario ufficiale riconciliato e review di sicurezza/provenienza.
- Identificazione e approvazione indipendente di immagine Debian, VM e snapshot; autorizzazione per i soli controlli preflight previsti.
- Approvazione esplicita del set di letture metadata-only per fixture D6, receipt, backup, log e stato guest; nessuna lettura dei payload D6.
- Approvazione distinta per staging/configurazione gateway e ciascun caso privilegiato, con hash, argomenti, allowlist e recovery plan esatti.
- Autorizzazione specifica per eventuale collector e directory di output.
- Autorizzazione separata per installazione, cambi amministrativi, rollback e release.

## Readiness separata e blocchi residui

| Dimensione | Valutazione M16 |
|---|---|
| Readiness tecnica locale | **Parziale**: closure M15 byte-consistente; M14 riporta build PASS e suite filtrata 134/134 PASS, ma qualification driver FAIL e 35 test NOT RUN (25 privilegiati, 5 scanner, 4 static/mock Alpha M1, 1 driver; il driver è stato eseguito separatamente e fallisce). Evidenze storiche, non test M16. |
| Integrità evidenze disponibili | **PASS limitato** per nove artefatti M14 verificati; **NOT VERIFIED** per completezza complessiva e prove protette/storiche. |
| Provenienza autenticata | **BLOCKED**: attestazioni/hash solo locali; 7 pin divergenti; nessuna catena firmata/indipendente o rebuild riproducibile accettato. |
| Readiness operativa VM | **BLOCKED**: hypervisor non interrogabile qui; VM/snapshot, risorse, isolamento e prerequisiti guest non verificati. |
| Autorizzazione campagna | **BLOCKED**: nessuna approvazione esatta disponibile; casi privilegiati non eseguiti. |
| Autorizzazione rilascio | **BLOCKED**: separata e non concessa. |

Blocchi residui che incidono direttamente sulla decisione: (1) 16 input non riconciliati nell'inventario ufficiale con driver in FAIL; (2) provenienza e pin binari 7/7 irrisolti; (3) D6 originale, fixture/evidence storiche e custody non riconciliate; (4) VM/snapshot Debian non identificati e non preflightati; (5) campagna privilegiata non eseguita e non autorizzata; (6) review indipendente finale assente. Non si elevano i normali risultati M14 PASS a gate Alpha M1.

## Proposta di autorizzazione limitata — NON CONCESSA

Proposta per futura decisione umana: autorizzare **solo** identificazione read-only e preflight della VM Alpha M1 Debian nominativamente identificata, su snapshot nominativamente identificato, con cattura in directory nuova privata `0700` sotto `/var/tmp/monad-alpha-tests`; limitare la lettura a identità VM/boot, OS/toolchain, risorse, filesystem/mount e metadati no-follow della fixture/D6 e delle prove nominate dal runbook. Nessun avvio se non già attiva e dimostrata non mutante; nessun test, collector, lettura payload D6, installazione, staging, gateway, modifica, rollback o scrittura nel guest. La richiesta deve specificare VM UUID, snapshot ID, operatore, comandi read-only esatti, percorsi inclusi, custode/output e reviewer. Questa proposta non autorizza da sé alcuna azione e non approva la campagna.

## Decisione finale

**NO-GO alla campagna di qualificazione Debian VM; Alpha M1 rimane BLOCKED.** La consistenza M15/M14 è sufficiente solo per confermare gli hash locali e la continuità byte degli input enumerati. Non è sufficiente per autenticare provenienza, dichiarare integre e complete tutte le evidenze storiche, approvare l'ambiente VM o eseguire casi privilegiati. Il prossimo gate utile è la riconciliazione revisionata dell'inventario e della provenienza, seguita da autorizzazione esplicita del preflight read-only sulla VM identificata. Nessuna campagna o rilascio è autorizzato da M16.
