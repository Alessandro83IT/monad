# Alpha M1 — Session 44: verified launch & host–guest evidence reconciliation

**Decision: BLOCKED.** La catena Python verificata è stata ricostruita e il codice inline del nuovo schema di lancio è stato confrontato byte per byte con la costante testata. I test sintetici passano. Rimangono però prerequisiti operativi non dimostrati nel container: binding della VM/snapshot al guest, checkout e runtime della VM, output/capture path preesistenti e approvati, read set completo e autorizzazione umana. Per questi motivi non è possibile produrre oggi un comando di acquisizione letterale senza valori inventati. Il comando derivato in [procedura verified launch](alpha-m1-session-44-verified-launch.md) è marcato NON ESEGUIRE e resta parametrico.

Alpha M1 non è qualificata. Installazione e test privilegiati restano NON AUTORIZZATI.

## Baseline Git e preservazione

- Branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`.
- Working tree già sporco: modifiche tracked, documenti/script/build directory e prove non tracked. Nessun file preesistente è stato ripristinato, cancellato o sovrascritto. In Sessione 44 sono stati creati solo i documenti Sessione 44 indicati in fondo.
- Sei artefatti Sessione 34B ricalcolati e corrispondenti ai digest storici; corpus storico ricalcolato PASS. Nessuno di questi file è stato modificato.
- Runner, collector e bootstrap non sono stati modificati. Nessuna azione sulla VM, file system guest, systemd, cgroup, mount o snapshot.

## Hash e controllo del corpus

| Componente | SHA-256 attuale | Esito |
|---|---|---|
| Inline loader dalla costante `INLINE_LOADER` | `feef002511128263daf5fa05fcaa72686a688d58fb717b2f8d1a9d026b205021` | 626 byte |
| Bootstrap | `3a5aa58d0a87b1363e61180773b4fd0318b930d41b81186a39ff0fd40a71f9a6` | PASS |
| Runner | `f67a5dde11add6ced5802fa8091680f75f480a43fcc386f2049844f313b2f469` | PASS, baseline immutabile |
| Collector | `6527d34cbfcca75244a77a1457f13e3d8e1b79aee5393f348c73bc96d74a74d2` | PASS, baseline immutabile |
| Verificatore storico | `b003d2b02dcbb02d304371524ef395838599fd918e1505bd100fc2a1340435e3` | PASS |
| Storico | 149 file, digest `f229f1b367f31053a283f564b3d2fa3edd713bfa1ede5134c899466f1bb8f67f` | PASS |

I sei digest 34B ricalcolati: installer `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7`; manifest `00fd4036bf0f3873f5c2a0e932c912b4709460fa52c35a1c2564016a92811262`; runner amministrativo `c68bca67a7a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def`; support `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514`; canary `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2`; bozza sudoers `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18`. Risultato: nessuna discrepanza locale. Questi digest non provano provenienza, approvazione umana o copia VM.

## Verified launch: implementazione, comando e limiti

La catena ricostruita è: loader inline fissato → bootstrap su stdin con SHA-256 verificato → runner aperto tramite componenti assoluti `O_NOFOLLOW`, catturato e compilato dagli stessi byte verificati → parsing argomenti runner → apertura/hash del solo collector fissato → copia su memfd → applicazione e verifica dei quattro seal → rehash memfd → `/bin/bash /proc/self/fd/N`. Il descriptor memfd è ereditato solo dal child previsto (`pass_fds`, `close_fds=True`). Nessun fallback senza memfd sealing o procfs.

La sequenza argomenti corretta, secondo il codice, è bootstrap digest, runner digest, path assoluto runner, poi `--execute --output-parent ABS_PARENT --candidate-dir ABS_CHECKOUT/qualification`. Il test della CLI di verifica usa un memfd fittizio e mocka `snapshot_collector`; il controllo Sessione 44 sul parser mocka sia snapshot sia execute. Un ulteriore test usa bootstrap e runner sintetici e conferma che gli argomenti arrivano invariati. Nessuno dei test apre il collector reale attraverso il runner.

La sorgente inline nel nuovo schema Sessione 44 è **byte-identica** alla costante testata: 626 byte, SHA-256 `feef002511128263daf5fa05fcaa72686a688d58fb717b2f8d1a9d026b205021`. I pin bootstrap e runner corrispondono ai file correnti. Questo chiude la discrepanza dei byte del comando rispetto al loader verificato. Il nuovo involucro shell aggiunge `set -o noclobber`, apertura fail-closed del descriptor status e redirezione a livello del compound pipeline, in modo che l'errore di `cat` sia acquisito e `pipefail` preservi il risultato.

**Non è stato prodotto un comando operativo letterale completo.** Non conosciamo il path reale/approvato del checkout guest né i due path di output già esistenti. La destinazione riportata nella Sessione 43 è assente e non è il parent esistente richiesto dal collector; non è noto né approvato un capture directory separato. Inventare path violerebbe i prerequisiti. Il template in procedura richiede quindi variabili esplicite e fallisce subito se non impostate; va sostituito con una copia a path letterali solo dopo preflight e review finale. Questa lacuna mantiene il gate aperto e impedisce READY FOR HUMAN APPROVAL.

Trust boundary residua: Python/interprete, standard library, loader dinamico, `cat`, `env`, shell, kernel e utilities/inputs del collector sono fidati ma non autenticati dai pin. `-I -S` ed `env -i` limitano l'ambiente/import, senza autenticare questi componenti. Non-root è imposto dal collector. Il runner non ha timeout interno; SIGINT/TERM/HUP sono inoltrati al process group. SIGKILL, perdita alimentazione o failure di processo/capture possono lasciare prove parziali. I controlli output sono pathname-based e non eliminano una race con un attore concorrente dello stesso UID; servono directory private e stabili, controllate dall'operatore. Nessun difetto nei byte esaminati richiede modifica di codice.

## Riconciliazione host–guest

Il container rilevato è `container-other`, non l'host `gentoo-msi` né la VM Debian; `VBoxManage` non è disponibile qui. I dati operatore (VM UUID/stato/snapshot, machine-id, boot-id, Debian 13, ext4 e percorso candidato) restano **RIFERITI**, non prove dirette di Codex.

La procedura read-only in [checklist host–guest](alpha-m1-session-44-host-guest-reconciliation.md) raccoglie `VBoxManage showvminfo` e lista snapshot/dischi sull'host, quindi OS/kernel, machine-id, boot-id, DMI, mount e filesystem dalla console guest già autorizzata. L'operatore collega i transcript indicando che la console visualizzata da VirtualBox appartiene alla VM UUID osservata nello stesso controllo host; confronta il DMI product UUID solo tramite una relazione firmware documentata, senza assumere equivalenza di formato con l'UUID VirtualBox. Machine-id/boot-id o hostname soli non legano la VM. Questo è un binding operativo attestato, non una prova crittografica prodotta dagli strumenti. Se la console o la relazione UUID resta ambigua, G6 resta BLOCKED. La snapshot corrente non prova che non vi siano state modifiche successive.

Nessuna verifica host o guest è stata eseguita da Codex e nessun output è stato scritto sulla VM. Il preflight propone solo terminale/transcript controllato, con stop e review sequenziale di path; non richiede sudo, non crea directory, non ricorre D6 e non avvia processi/test.

## Matrice aggiornata dei gate

| Gate | Stato Sessione 44 | Evidenza / condizione per chiuderlo |
|---|---|---|
| Runner/bootstrap/collector hash locali | **PASS locale** | Ricalcolati contro pin completi; nessuna modifica |
| Corpus storico 149 / digest fisso | **PASS locale** | Verificatore read-only PASS |
| Sei artefatti 34B | **PASS locale** | Sei digest ricalcolati e allineati |
| Loader inline nel launch doc | **PASS** | 626 byte identici a `INLINE_LOADER`, digest noto |
| Argomenti bootstrap → runner e parsing CLI | **PASS sintetico** | Forwarding testato con payload sintetico; parser testato con execute mockato |
| Byte runner eseguiti uguali ai byte verificati | **PASS tecnico condizionale** | Bootstrap cattura/hash/compila gli stessi byte |
| Collector eseguito dai byte sigillati | **PASS tecnico condizionale** | Codice richiede memfd, quattro seal e rehash; no real execution |
| Test suite sintetica Sessione 44 | **42 PASS** | 0 FAIL, 0 SKIP, 0 TIMEOUT |
| Comando shell finale a path letterali | **BLOCKED** | Checkout guest, output parent e capture dir approvati non disponibili; pipeline integrale non eseguita |
| Host VirtualBox / VM UUID / snapshot | **BLOCKED** | Dati solo riferiti; manca transcript host accessibile |
| Binding VM ↔ guest | **BLOCKED** | Nessuna attestazione console stessa-sessione né DMI reconciliation verificata |
| Runtime, checkout e accesso read set sulla VM | **BLOCKED** | Guest non accessibile in questo container |
| Output parent e capture directory | **BLOCKED** | Stato guest, ACL/mount, ownership e nomi liberi non verificati/approvati |
| Approvazione del read set completo | **BLOCKED** | Nessuna review umana puntuale ricevuta |
| Autorizzazione acquisizione | **BLOCKED** | Modello vuoto; approvatore non ha autorizzato |
| Collector reale / runner `--execute` / VM / CTest privilegiato | **NOT RUN** | Divieti rispettati |
| Campagna storica Alpha M1 | **34 BLOCKED / 7 NOT RUN** | Invariate, nessuna riclassificazione |
| Installazione/canary/rollback/cleanup/admin | **NOT RUN / VIETATI** | Nessuna autorizzazione |

## Test consentiti: esito

Comando eseguito:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v \
  tests.alpha_m1_evidence_tree_digest_test \
  tests.alpha_m1_verified_evidence_runner_test \
  tests.alpha_m1_verified_execution_bootstrap_test
```

Esito: **42 PASS, 0 FAIL, 0 SKIP, 0 TIMEOUT**. Copertura rilevante: hash/size inline e bootstrap; digest errati; path relativi/traversal/symlink/file speciali; sostituzione/mutazione concorrente; import isolation; CLI verification-only con snapshot mockato; marker bootstrap mancante; parser arguments non supportati; fixture memfd/seal e runner solo su script sintetici; segnali/errore/status; digest tree sintetico. Il test di invocazione diretta del runner si ferma prima di accedere al collector; nessun test ha letto/aperto il collector reale attraverso il runner.

Test supplementari ad hoc senza file persistenti: parser `--execute` valido con snapshot/execute mockati; argv `--execute` incompleto rifiutato prima dello snapshot; forwarding esatto degli argomenti tramite bootstrap verso runner sintetico: **PASS**. Verifica testuale della sorgente inline nel launch doc vs costante test: **PASS**, byte-identica e hash atteso. Sono controlli di codice/argomenti; non testano host, guest, runtime VM o collector.

## Revisioni indipendenti

I rapporti separati sono [Core](alpha-m1-session-44-core-review.md), [Security](alpha-m1-session-44-security-review.md), [Verification](alpha-m1-session-44-verification-review.md) e [Operations](alpha-m1-session-44-operations-review.md).

- **Core — PASS tecnico:** ha controllato mapping argv inline loader → bootstrap → runner → collector e digest sorgente. Il loader in template corrisponde alla costante; nessun difetto di implementazione o necessità di modificare codice. Il comando operativo resta BLOCCATO finché non ci sono path guest letterali approvati.
- **Security — PASS statico sul template rivisto; BLOCKED operativo:** la prima review aveva trovato che l'apertura status FD non falliva esplicitamente chiusa e che l'errore `cat` non era catturato. La versione Sessione 44 apre FD3 con guard prima del pipeline e redirige tutto il compound command. Il reviewer ha confermato la chiusura del rilievo e la corrispondenza dei byte inline; resta il trust boundary Python/runtime/kernel/strumenti e la race pathname con attore dello stesso UID.
- **Verification — PASS sintetico:** pin e corpus riconfermati; ha eseguito 40 test sintetici selezionati e ha omesso due test per prudenza (NOT RUN). Il coordinatore ha eseguito la suite mirata completa di tre moduli: 42 PASS. Né esecuzione ha letto il collector reale attraverso il runner.
- **Operations — PASS per la procedura di preflight; BLOCKED per acquisizione:** checklist host/guest e gestione errori coerenti. La stessa console VirtualBox è attestazione operativa umana, non prova crittografica; DMI necessita mapping documentato e l'ambiguità mantiene G6 BLOCKED. Path finali, hash/runtime guest e read scope devono essere verificati sul guest.

**Revisione integrata finale:** le quattro valutazioni sono coerenti: design byte-verificato tecnicamente accettabile entro il trust boundary dichiarato, ma il comando rimane un template e mancano prove operative. Nessun reviewer autorizza l'esecuzione. Il dossier resta BLOCKED.

## Modello di autorizzazione — una sola acquisizione diagnostica

```text
Richiedente/operatore e ID: __________________________________________
Approvatore responsabile/ruolo: ______________________________________
Data/ora UTC e finestra: ______________________________________________
Host VirtualBox e transcript: ________________________________________
VM nome/UUID/stato e transcript: ______________________________________
Snapshot corrente UUID/nome e prova: __________________________________
Guest OS/kernel, machine-id, boot-id, DMI e transcript console: _________
Relazione host↔guest attestata, operatore e riferimenti: _______________
Checkout guest assoluto, branch, HEAD: _________________________________
Inline loader SHA-256: feef002511128263daf5fa05fcaa72686a688d58fb717b2f8d1a9d026b205021
Bootstrap SHA-256: 3a5aa58d0a87b1363e61180773b4fd0318b930d41b81186a39ff0fd40a71f9a6
Runner SHA-256: f67a5dde11add6ced5802fa8091680f75f480a43fcc386f2049844f313b2f469
Collector SHA-256: 6527d34cbfcca75244a77a1457f13e3d8e1b79aee5393f348c73bc96d74a74d2
Storico: 149 file / f229f1b367f31053a283f564b3d2fa3edd713bfa1ede5134c899466f1bb8f67f — esito/ora: ______
Read set intero Sessions 37/38/40 revisionato (riferimenti): __________
Evidence parent preesistente/path/owner/mode/ACL/mount: _________________
Capture dir distinta preesistente/path/mode 0700/ACL/mount: ____________
Nomi stdout/stderr/status assenti e riservati: _________________________
Utente guest non-root: ___________________ Numero run: UNA SOLA
Comando completo a path letterali allegato e revisionato: [ ]
Piano custodia/retention/trasferimento output: _________________________

DECISIONE: [ ] APPROVO una sola acquisizione [ ] NEGO [ ] RINVIO
Firma/registrazione approvatore e timestamp UTC: _______________________
Scope consentito: un solo runner verificato + collector fissato, read set
sopra e nuovi output solo nei path elencati. Nessuna estensione implicita.

STOP e preserva parziale senza retry/repair/cleanup per: qualsiasi mismatch
identità/hash; binding non dimostrato; symlink/tipo/owner/mode/ACL/mount
inattesi; richiesta di privilegi; read error; output collision; segnale,
timeout, child non terminato, exit/status/record ambiguo; mutazione D6,
fixture, systemd, cgroup, mount, checkout o storico.
Non autorizza test/CTest, canary, staging, installazione, rollback, cleanup,
azioni VM/snapshot o modifiche amministrative.
```

## Artefatti Sessione 44

- Questo report: matrice, esiti e sintesi review.
- [Procedura verified launch](alpha-m1-session-44-verified-launch.md): catena, forma di comando NON ESEGUIRE, stop/error handling.
- [Checklist host–guest](alpha-m1-session-44-host-guest-reconciliation.md): query read-only e binding richiesto.
- Rapporti review: [Core](alpha-m1-session-44-core-review.md), [Security](alpha-m1-session-44-security-review.md), [Verification](alpha-m1-session-44-verification-review.md), [Operations](alpha-m1-session-44-operations-review.md).

Nessun report storico è stato sovrascritto. Nessun commit o push.

## Decisione finale

**BLOCKED.** I risultati tecnici locali consentono una futura review del comando completo, ma host–guest, path/runtime VM e autorizzazione mancano. Completare prima i preflight in una sessione autorizzata, fissare i path letterali e ottenere review finale del comando; poi il responsabile umano potrà decidere separatamente se autorizzare una singola acquisizione diagnostica non-root. Nessun runner `--execute`, collector, test privilegiato, installazione, canary, rollback, cleanup o mutazione amministrativa è autorizzato da questo report.
