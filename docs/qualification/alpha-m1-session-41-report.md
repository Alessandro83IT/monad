# Alpha M1 — Session 41: VM preflight, evidence authentication e acquisizione

**Decisione: BLOCKED.** Le verifiche locali di integrità hanno superato i controlli previsti, ma questo processo è in un container e non ha osservato né l'host VirtualBox né la VM Debian autorizzata. Non sono disponibili una riconciliazione host/guest, una destinazione di acquisizione approvata o un'autorizzazione umana specifica. Il runner è stato invocato solo nel test di verifica senza `--execute`, che ha aperto, hashato e copiato il collector reale in un memfd sigillato. Il collector non è stato eseguito. Alpha M1 resta **BLOCKED** e l'installazione **NON AUTORIZZATA**.

Durata effettiva: circa 20 minuti. L'attività è terminata dopo le verifiche necessarie; non è stato usato tutto il tempo pianificato.

## Baseline e confini

- Repository: `/home/alessandro/Projects/monad`; branch `main`; HEAD `abd6bac4b368427b76f6c98522afde928b418939`.
- Working tree già non pulito al preflight: modifiche tracked in `CMakeLists.txt` e file sotto `tests/`; numerosi artefatti, report, build directory e script non tracciati, inclusi quelli delle Sessioni 34B–40. Sono stati trattati come lavoro preesistente. Non sono stati ripristinati, cancellati o sovrascritti.
- Ambiente osservato: Debian GNU/Linux 13.7, kernel `6.12.113+deb13-amd64`, x86_64, `systemd-detect-virt=container-other`, uid/gid `1000:1000`. `VBoxManage` non è disponibile. Questi dati descrivono il container Codex e non la VM target.
- Letti i documenti obbligatori della Sessione 40, 36 e 34, il verificatore storico e il codice del runner. Esaminati i report delle Sessioni 35 e 34B per riconciliare conteggi, hash e limiti.
- Non sono stati modificati i sei artefatti candidati Sessione 34B, i 149 file storici o il runner Sessione 40.

## Verifiche di integrità

| Oggetto | Verifica corrente | Esito e limite |
|---|---|---|
| Runner diagnostico Sessione 40 `qualification/alpha-m1-verified-evidence-runner.py` | SHA-256 `468756ff8cc1f55e09e4560ec1e47974b53a076219715fa35bf36874fb2cc618`; regular file, 1 link, uid/gid 1000:1000, mode 0664 | **PASS locale** contro il digest completo riportato dalla Sessione 40. Il digest non autentica da solo chi ha prodotto o approvato il file e non prova quale copia sia sulla VM. |
| Collector Sessione 37 `qualification/alpha-m1-vm-evidence-collector.sh` | SHA-256 `6527d34cbfcca75244a77a1457f13e3d8e1b79aee5393f348c73bc96d74a74d2`; regular file, 1 link, uid/gid 1000:1000, mode 0664 | **PASS locale** contro il pin del runner e le baseline Sessioni 37/40. Nessuna esecuzione. |
| Storico Sessione 35 | `python3 -I qualification/alpha-m1-evidence-tree-digest.py` ha restituito PASS: 149 file e SHA-256 `f229f1b367f31053a283f564b3d2fa3edd713bfa1ede5134c899466f1bb8f67f` | **PASS locale**. Il verificatore rifiuta symlink/file speciali e cambiamenti concorrenti osservabili. Il framing non include metadati o directory vuote e non è una firma esterna. |
| Sei artefatti candidati Sessione 34B | Hash correnti sotto | **PASS di integrità locale** contro i digest del runbook/manifest/rapporti 34B–36. Provenienza e autenticità sono limitate ai documenti locali; non dimostra trasferimento o installazione sulla VM. |

Hash dei sei candidati ricalcolati direttamente:

| Artefatto | Percorso repository | SHA-256 | Tipo / metadati osservati |
|---|---|---|---|
| Installer | `qualification/install-athena-alpha-m1-admin.sh` | `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7` | regular, 1 link, 0755, 1000:1000 |
| Manifest | `qualification/alpha-m1-build-reconciliation-session34b-candidate.json` | `00fd4036bf0f3873f5c2a0e932c912b4709460fa52c35a1c2564016a92811262` | regular, 1 link, 0600, 1000:1000 |
| Runner amministrativo | `qualification/athena-alpha-m1-runner.py` | `c68bca67a7a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` | regular, 1 link, 0755, 1000:1000 |
| Supporto gateway | `qualification/gateway_support.py` | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` | regular, 1 link, 0664, 1000:1000 |
| Canary | `qualification/athena-alpha-m1-systemd-canary.py` | `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` | regular, 1 link, 0755, 1000:1000 |
| Draft sudoers | `qualification/athena-alpha-m1-sudoers` | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` | regular, 1 link, 0664, 1000:1000 |

Il manifest Sessione 34B ricalcolato è `00fd4036bf0f3873f5c2a0e932c912b4709460fa52c35a1c2564016a92811262`, uguale alla baseline. Non sono state dedotte identità da hash abbreviati. Gli artefatti locali sono modificabili dall'utente che li possiede; la verifica va ripetuta nel checkout/copia approvati immediatamente prima di una futura decisione.

## Coerenza del dossier storico

- Sessione 35 e il record riepilogativo Sessione 36 riportano 150 controlli: **109 PASS, 34 BLOCKED, 7 NOT RUN**, nessun FAIL/TIMEOUT. La somma è coerente. La Sessione 36 dettaglia i 34 controlli bloccati e i 7 non eseguiti. Questa sessione non riclassifica controlli.
- Sessione 36 riporta i sei hash candidati come corrispondenti ai pin Sessione 34B. I sei hash locali ricalcolati in Sessione 41 coincidono. Questo dimostra corrispondenza dei byte rispetto ai riferimenti locali, non autenticità indipendente o presenza sulla VM.
- Le baseline build riportano 140 test protetti e 147 isolati, entrambi con 18 nomi privilegiati nel manifest. La differenza 140/147 resta non spiegata; l'inventario dei nomi nel manifest non prova l'inventario CTest generato. Non è stato lanciato CTest.
- I due build attesi (`/opt/athena-qualification/build`, `/tmp/monad-s32-verification-final4-20261021`) sono visibili come directory nel namespace corrente, ma sono osservazioni del container. Non sono state usate per attribuire build alla VM e non sono stati eseguiti binari.
- `namei -l` sui percorsi locali del runner, del corpus storico e dei due build non ha mostrato componenti symlink. Le 149 evidenze sono file regolari, uid/gid 1000:1000, mode 0600; il root storico è directory 0700 uid/gid 1000:1000. I build sono directory: `/opt/athena-qualification/build` mode 0550 con ownership mappata 65534:65534 nel container; `/tmp/monad-s32-verification-final4-20261021` mode 0775 uid/gid 1000:1000. Questi metadati del namespace non descrivono la VM.
- Sessione 36 documenta che l'identità snapshot non fu osservata e che la provenienza sorgente-binario era incompleta. Sessione 40 mantiene la provenienza parziale localmente e non verificabile per gli artefatti VM attesi. Nessuna nuova evidenza chiude questi limiti.
- Il report Sessione 39 conserva correttamente la fotografia storica del runner precedente. Per la decisione corrente prevale l'hash finale completo della Sessione 40, ricalcolato sopra; i documenti storici non sono stati riscritti.

## Audit del runner e della procedura

La lettura del codice conferma: collector fisso e digest incorporato; apertura del collector senza seguire il symlink finale; copia in `memfd` con `MFD_CLOEXEC | MFD_ALLOW_SEALING`; verifica dei quattro seal; rehash del descriptor sigillato prima del lancio; argomenti CLI ristretti; `/bin/bash` fisso; ambiente figlio allowlistato; inoltro di SIGINT/SIGTERM/SIGHUP al process group; status/record restituito al chiamante. La suite sintetica Sessioni 39/40 è stata ripetuta in questa sessione.

Limiti rilevanti: il runner non autentica da sé il proprio file prima che l'interprete Python lo apra; hashare il pathname e poi invocarlo lascia una finestra di sostituzione del runner, se un attore concorrente può scrivere la directory. La garanzia del memfd riguarda i byte del collector, non i byte del runner, Bash, kernel, strumenti figli, input secondari o output. Il record su stderr non è durevole né anti-manomissione. Il descriptor sealed è passato a Bash ed è potenzialmente ereditabile dai processi figli. La cattura degli output e i percorsi privati/no-clobber restano responsabilità della procedura. La procedura Sessione 40 richiede percorsi preesistenti e `noclobber`, ma questa sessione non ha verificato proprietà, ACL, mount o assenza di symlink delle destinazioni sulla VM.

Non è stata modificata l'identità del runner. Per un futuro lancio, l'operatore deve assicurare che i byte del runner sottoposti ad approvazione siano anche quelli letti dall'interprete, tramite un supporto di esecuzione immutabile e controllato o una directory di provenienza protetta e verificata senza finestra di sostituzione. Non si deve presentare il solo `sha256sum` seguito da esecuzione del pathname come garanzia anti-TOCTOU del runner.

## Test eseguiti

Comando: `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v tests.alpha_m1_evidence_tree_digest_test tests.alpha_m1_verified_evidence_runner_test`

Risultato: **20 PASS, 0 FAIL, 0 SKIP**, exit 0. Le fixture avversariali sono sintetiche, ma la suite include il caso preesistente `test_fixed_collector_verification_only_cli`, che chiama `runner.main([])` sul percorso collector reale. Ha eseguito solo la verifica/copia in memfd e non ha usato `--execute`; il collector non è stato eseguito. La suite quindi non è interamente fixture-only, ma rispetta il divieto di eseguire il collector. Copertura inclusa: framing e determinismo del tree digest, root inattesa, file mancanti/aggiuntivi, symlink/file speciali, race su file/directory; runner con digest corretto/errato, file mancanti/illeggibili, symlink/hardlink, sostituzione/modifica concorrente, descriptor non valido/non sealed, ambiente contaminato, segnali/processo figlio, output/status nonzero e interfaccia CLI limitata.

Comando di verifica storica: `python3 -I qualification/alpha-m1-evidence-tree-digest.py | tail -1` — **PASS**, 149 file, digest atteso. Non è stata fatta un'invocazione CLI manuale separata del runner; il test `test_fixed_collector_verification_only_cli` ha invocato `runner.main([])` in modalità verification-only, come specificato sopra.

## Matrice di evidenza e gate

| Gate / requisito | Stato | Classe della prova | Motivo |
|---|---|---|---|
| G1 — Collector e digest | PASS locale | Prova disponibile e verificata localmente | SHA-256 pieno coincide con pin Sessione 37/40; VM copy non verificata. |
| G2 — Runner e digest | PASS locale con limite di launch | Prova disponibile e verificata localmente | Hash pieno coincide con Sessione 40; runner pathname non è auto-autenticante contro sostituzione dopo hash. |
| G3 — Storico 149 file | PASS locale | Prova disponibile e verificata localmente | Count e digest riprodotti; non è attestazione indipendente e non copre metadata. |
| G4 — Garanzie runner | BLOCKED per l'esecuzione | Prova disponibile ma incompleta; revisione Security | Seal/re-hash del collector superano i test, ma il pathname del runner può essere sostituito tra hash e apertura da Python. I byte runner approvati non sono legati ai byte caricati; il metodo di launch immutabile va progettato e revisionato separatamente. |
| G5 — Output privati e tracciabili | BLOCKED | Prova non disponibile | Nessuna directory capture VM identificata, verificata o approvata. |
| G6 — VM esatta e snapshot | BLOCKED | Prova non disponibile | Host VirtualBox non accessibile; container non prova identità guest. |
| G7 — Accessi guest e read scope | BLOCKED | Prova non disponibile | Nessuna sessione guest autorizzata o permessi VM osservati; lo scope completo del collector richiede approvazione esplicita. |
| G8 — Procedura read-only | PARZIALE | Procedura disponibile; verifica operativa non eseguita | Checklist manuali predisposte qui; ancora da eseguire e revisionare con output del vero host/guest. |
| G9 — Provenienza dei build | PARTIAL / BLOCKED per qualificazione | Prova disponibile ma non autenticata | Le storie 34B–40 riportano cache/hash; non legano in modo autenticato sorgenti, toolchain, invocazione e binari. La raccolta diagnostica non richiede di promuovere tali build, ma la qualificazione li richiede. |
| G10 — Autorizzazione umana | BLOCKED | Approvazione umana necessaria | Nessuna autorizzazione specifica è stata rilasciata; Codex non la presume. |
| Test della campagna / installazione | NOT RUN | Controllo non eseguito | Divieto esplicito; invariati i 34 BLOCKED e 7 NOT RUN storici. |

**Decisione Sessione 41: BLOCKED.** Non siamo ancora a “READY FOR HUMAN APPROVAL”: mancano prove verificabili su host, guest, snapshot, read scope e destinazioni output, oltre alla soluzione operativa per garantire i byte del runner eseguiti. Per rimuovere il blocco occorre completare le checklist host e guest da un host autorizzato e dalla VM esatta, riconciliare UUID/snapshot con machine-id/boot-id attraverso una sessione verificabile, fissare e ispezionare directory output già esistenti, approvare l'intero read set e risolvere il launch immutabile del runner. Quindi un responsabile umano può decidere separatamente se autorizzare una sola acquisizione diagnostica. Tale decisione non autorizza test, staging, canary o installazione.

## Revisioni indipendenti

**Core:** conferma che i sei percorsi/hash 34B sono coerenti e che le query host/guest sono read-only; ha chiesto pausa/review dopo `namei` prima di `stat` e ha notato che `hostnamectl`/`date --iso-8601` presuppongono host GNU/Linux. Questi chiarimenti sono stati applicati. **Verification:** conferma 20/20 PASS e digest storico PASS; il test CLI verification-only legge/hash/seala il collector reale, senza `--execute`. **Security:** conferma che sealing e rehash proteggono il collector; il runner stesso non è legato ai byte eseguiti se viene hashato e poi aperto da pathname scrivibile. Raccomanda un launcher separatamente fidato o storage immutabile verificato. **Operations:** ha espresso NO-GO e ha rilevato gli stessi problemi di sequencing, poi corretti nel guest checklist; non ha restituito una nuova conferma post-correzione. Nessuna revisione costituisce autorizzazione all'esecuzione.

## File della Sessione 41

Creati:

- `docs/qualification/alpha-m1-session-41-report.md`
- `docs/qualification/alpha-m1-session-41-host-preflight.md`
- `docs/qualification/alpha-m1-session-41-guest-preflight.md`
- `docs/qualification/alpha-m1-session-41-diagnostic-authorization.md`

Non sono stati modificati runner, collector, artefatti Sessione 34B, report storici, corpus dei 149 file, build o file amministrativi. Nessun comando privilegiato, test Alpha/CTest, runner con `--execute`, esecuzione del collector, installazione, canary, rollback, cleanup, mutazione systemd/cgroup/mount, ripristino snapshot, commit o push è stato eseguito. Il test runner verification-only citato sopra ha letto/hashiato il collector e ne ha copiato i byte in un memfd.
