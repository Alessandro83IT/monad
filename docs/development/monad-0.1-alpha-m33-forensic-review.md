# Monad 0.1 Alpha M33 — Alpha M1 Administrative Forensic Reconstruction

**Verdetto: NO-GO / NOT QUALIFIED.** L'acquisizione M32.1 riferita è `PARTIAL`; `/usr/local/sbin/gateway_support.py` è segnalato `REQUIRED_ABSENT`. Il runner riferito sulla VM ha digest diverso dal pin dell'installer corrente e coincide con il digest riportato in M25/M27. Questi elementi descrivono una discrepanza amministrativa concreta, ma non ne provano la causa né la provenienza. Non ho eseguito operazioni sulla VM o su percorsi amministrativi. Nessuna installazione, rollback, canary o qualificazione è stata eseguita.

## 1. Executive summary

L'evidenza riferita per M32.1 dichiara integrità interna di `inventory.json` tramite `SHA256SUMS`, schema 4, 598 entry, 1176 entry di enumerazione consumate e zero byte sottoposti a hash. L'unica anomalia riportata è l'assenza del gateway support nella destinazione installata. Il file è inoltre riferito assente nella staging directory. L'hash del runner installato riferito è `5d6ee35070954d97034de7f0323204995fe0ca86ed12fad774a3acb9ada021d5`; quello del runner sorgente/pin corrente è `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def`. Il support locale e il pin concordano a `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514`.

La discrepanza è coerente con un'installazione incompleta, una generazione precedente, una rimozione successiva o una divergenza fra codice storico e installer attuale. Le evidenze disponibili non discriminano fra queste cause. La directory backup nominata non è stata acquisita integralmente: una ricerca fino a profondità tre senza risultati per nomi specifici non prova che transazioni o marker non esistano. Il digest del runner coincidente con M25 è un collegamento di contenuto riportato, non una catena di provenienza.

Il collector applica correttamente `REQUIRED_ABSENT` al leaf richiesto mancante: quell'etichetta non dichiara che il file sia stato storicamente assente né che una specifica transazione sia avvenuta. È un blocker per la completezza dell'acquisizione e per la verifica dello stato installato; non è una diagnosi causale.

## 2. Scope e metodologia

- Stato iniziale verificato prima del lavoro: branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`, tracking `origin/main`. Il workspace aveva numerose modifiche tracked e file/evidenze/build non tracciati. Sono stati preservati; il rapporto richiesto non esisteva prima della scrittura.
- Gli artefatti `qualification/` e gran parte della documentazione M19–M32 sono file locali non tracciati nello stato Git iniziale. Non sono quindi attribuibili al commit HEAD né autenticati dalla cronologia del repository.
- Ho letto i sorgenti e i rapporti locali, calcolato SHA-256 dei quattro artefatti richiesti e letto il collector M32.1 e i suoi test. La cronologia Git disponibile per il percorso `qualification/` non contiene la storia utile degli script locali: l'HEAD risale al 20 ottobre e il log di path non documenta queste generazioni.
- Non ho usato `sudo`, richiesto privilegi o tentato accessi che aggirino permessi. Nessun file amministrativo, collector, baseline, pin o evidenza preesistente è stato modificato. Non ho eseguito CTest perché i test interessati sono suite Python sintetiche e il cambiamento di questa sessione è solo documentale.
- Classi adottate: **Verificato localmente** (ripetuto in questo workspace); **Dichiarato dall'operatore** (informazione fornita per M32.1, non riacquisita); **Documentato** (un rapporto locale afferma un fatto); **Inferenza**; **Sconosciuto/bloccato**. SHA256SUMS verifica coerenza dei file inclusi, non autenticità del collector, dell'host o della catena di custodia.

## 3. Inventario delle evidenze

| Evidenza | Stato e affidabilità | Cosa consente di concludere |
|---|---|---|
| Questo checkout: branch, HEAD e status iniziale | Verificato localmente | Baseline del workspace; non identifica la VM né autentica file untracked. |
| Script amministrativi correnti in `qualification/` | Lettura e hash locali verificati; file untracked | Contratto implementato dal contenuto presente, non necessariamente quello installato o usato storicamente. |
| `docs/development/monad-0.1-alpha-m27-forensic-reconciliation.md` | Documento locale, untracked, origine non autenticata | Registra osservazioni M27 riferite, fra cui runner hash `5d6ee…21d5`, support assente, sudoers digest e marker incompleto. Prova che il documento contiene queste affermazioni. |
| Rapporti M19, M28–M32.1 e documenti qualification | Documenti locali, perlopiù untracked; nessuna firma/custodia disponibile | Ricostruiscono evoluzione dei candidati e limiti dichiarati, non dimostrano gli eventi VM. |
| Acquisizione M32.1: directory `/var/lib/monad-m32-1-evidence-20261029T194120Z` | Dichiarata dall'operatore; non accessibile/verificata in questa sessione | Schema 4, 598 entry, 1176 enumeration, 0 byte hashed, `REQUIRED_ABSENT` support, `PARTIAL`, checksum interno dichiarato verificato. |
| Runner installato e stage sulla VM | Dichiarato dall'operatore e coerente con M27 documentato | Digest runner `5d6ee…21d5`; support assente in entrambe le posizioni dichiarate. Mancano output grezzi, metadata, firma e custodia. |
| Backup `/var/lib/athena-alpha-m1/backups/run-20261021-061921` | Dichiarato dall'operatore; inventario incompleto | Una ricerca di nomi fino a profondità tre non ha trovato i tre pattern specificati. Non dimostra che la transazione manchi o sia incompleta. |

**Evidenza diretta VM non acquisita qui.** Non posso verificare permessi, identità VM/snapshot/boot, contenuti grezzi, ACL, metadata o provenienza degli output riferiti.

## 4. Cronologia amministrativa ricostruita

### Fatti verificati nel workspace

1. HEAD è `abd6bac4…8939` (20 ottobre 2026); i file amministrativi correnti sono file locali non tracciati. Il log Git interrogato non restituisce una cronologia di modifiche per questi file locali.
2. L'installer attualmente letto definisce tre target: runner `/usr/local/sbin/athena-alpha-m1-runner`, support `/usr/local/sbin/gateway_support.py`, sudoers `/etc/sudoers.d/athena-alpha-m1`. Il canary è un prerequisito in staging, non un target installato.
3. Il rapporto M27 locale riporta l'hash `5d6ee…21d5` per il runner VM e l'assenza del support; il rapporto M19 aveva già identificato le stesse classi di discrepanza sulla base di osservazioni M18. Queste sono affermazioni storiche in documenti locali non autenticati.
4. M27 documenta un backup con `runner.absent`, `sudoers.absent` e marker `install-started` di zero byte nella directory indicata, ma non presenta un inventario completo. L'installer corrente invece richiede tre stati di presenza originaria e scrive marker start con ID transazione.
5. M28–M32.1 documentano l'evoluzione dei collector read-only, i relativi test sintetici e le classificazioni dei percorsi. M32.1 aggiunge i limiti di enumerazione e fsync del parent.
6. I dati M32.1 forniti in questa richiesta riportano il medesimo runner hash storico, l'assenza del support in target e stage e l'assenza dei pattern di transazione nella ricerca dichiarata. Sono dati riferiti, non verificati in questa sessione.

### Sequenza di generazioni: limite della ricostruzione

- **Generazione storica identificata solo dal digest:** runner riferito `5d6ee…21d5`, presente almeno nelle ricognizioni M25 e M27 secondo i resoconti disponibili. Non è stato trovato qui uno snapshot sorgente o un commit che lo produca. La corrispondenza M25/M27/M32.1 non autentica che il file sia invariato dall'origine o che quelle ricognizioni siano indipendenti.
- **Generazione corrente del checkout:** runner `c68b…9def`, support `92c9…d514`, sudoers `263c…6c18`, tutti coerenti con i pin del file installer locale. Il checkout non prova quando né se questa generazione sia stata trasferita sulla VM.
- **Transizione fra le due:** non ricostruibile. Non sono disponibili installer storico autenticato, stage files grezzi, record transazione validato, backup completo, audit log con custodia, snapshot VM confrontabili o prova temporale affidabile.

Non affermo che M25 abbia installato un runner, che un'installazione sia stata interrotta, o che il gateway sia stato rimosso: i materiali non provano questi passaggi.

## 5. Matrice degli hash

| Artefatto | Percorso | Hash atteso / riferimento | Hash osservato | Origine dell'hash | Verifica | Affidabilità provenienza |
|---|---|---|---|---|---|---|
| Installer | `qualification/install-athena-alpha-m1-admin.sh` | Nessun self-pin nel file | `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7` | `sha256sum` locale | Verificato localmente | Media per identità byte nel workspace; bassa per release/origine perché file untracked. |
| Runner sorgente corrente | `qualification/athena-alpha-m1-runner.py` | `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` | stesso | Hash locale e literal installer | Verificato localmente: pin = sorgente | Media per contenuto; non lega il sorgente alla VM. |
| Runner installato | `/usr/local/sbin/athena-alpha-m1-runner` | M25/M27: `5d6ee35070954d97034de7f0323204995fe0ca86ed12fad774a3acb9ada021d5`; corrente `c68b…9def` | `5d6ee…21d5` riferito | M32.1 fornito; M27 documento locale | Non verificabile localmente; diverso dal pin attuale | Bassa: output grezzo/custodia non disponibili; coincidenza digest non autentica. |
| Gateway support sorgente | `qualification/gateway_support.py` | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` | stesso | Hash locale e literal installer | Verificato localmente | Media per contenuto; file untracked, provenienza di rilascio non dimostrata. |
| Gateway support installato | `/usr/local/sbin/gateway_support.py` | Pin corrente `92c928…d514` | assente riferito | Inventario M32.1 fornito | Non verificabile localmente; status riferito REQUIRED_ABSENT | Bassa per provenienza, ma la dichiarazione segnala un gap operativo rilevante. |
| Gateway support in stage | `/root/athena-alpha-m1-stage/gateway_support.py` | Input previsto, pin `92c928…d514` | assente riferito | Inventario M32.1 fornito | Non verificabile localmente | Bassa; stage è condizionale dopo installazione, ma assenza non spiega il target. |
| Sudoers sorgente | `qualification/athena-alpha-m1-sudoers` | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` | stesso | Hash locale e literal installer | Verificato localmente | Media per contenuto. |
| Sudoers installato | `/etc/sudoers.d/athena-alpha-m1` | M27 afferma `263c…6c18`; corrente stesso | M32.1 non riferisce hash | Rapporto M27 non autenticato | Non verificato in M32.1 presentata | Bassa; non assumere stato corrente. |
| Canary sorgente | `qualification/athena-alpha-m1-systemd-canary.py` | `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` | Hash corrente menzionato da M19/M27, non ricalcolato in questa matrice | Literal e rapporti locali | Nessun confronto VM corrente | Bassa per VM; canary non è installato dal installer. |
| `install-started` storico | `…/run-20261021-061921/install-started` | Formato corrente: `transaction=<id>\n` | Zero byte secondo M27; nessuna verifica M32.1 | Documento M27 | Contrasto documentato, bytes non accessibili | Bassa; può essere versione diversa, omissione o modifica successiva. |
| Inventario M32.1 | `…/inventory.json` e `SHA256SUMS` | Il checksum contenuto nel bundle | Dichiarato verificato dall'operatore | Report M32.1 fornito | Non verificato localmente | Conferma interna solo se il bundle è integro; non autentica acquisizione/origine. |

La coincidenza tra digest installato riferito e digest storico riferito dimostra al più un'identità di byte se entrambe le misurazioni sono corrette. Non prova firma, autore, data di installazione, build riproducibile, autorizzazione, transazione o catena di custodia.

## 6. Analisi installer e runner

### Prerequisiti e autenticazione input

L'installer attuale richiede root fuori test mode e controlla componenti di path, proprietà, mode, symlink, ACL e file regolari. Verifica i digest di runner/support/sudoers/canary stage contro literal nel file, e sette artefatti sotto `/opt/athena-qualification/build`. Verifica inoltre tool e path, utente `alessandro`, directory protette, fixture ext4 vuoto, backup root e receipt canary fresca, legata a runner/canary hash, boot ID e versione systemd attuali. Il test mode usa un root temporaneo caller-owned mode 0700 solo da non-root e redirige i target; non è stato eseguito in questa sessione.

I literal sono pin di contenuto, non firme. Non autenticano da soli lo script installer né la sua fonte. Un hash hardcoded non sostituisce la provenienza indipendente del candidate bundle.

### Backup e transazione

Prima della pubblicazione, il codice cattura ciascuno dei tre target come `<key>.existed` più payload/sidecar (mode, sha256, owner-group), oppure `<key>.absent`; genera manifest e `transaction.json`, quindi `prepared`. I file sono creati con modalità restrittive e fsync. Verifica coerenza di bytes/metadati prima di generare il transaction record. `install-started` contiene l'ID transazione ed è fsyncato prima della prima rinomina.

Le destinazioni vengono pubblicate separatamente con rename atomico per singolo file e fsync delle directory, nell'ordine runner, support, sudoers. Non esiste atomicità congiunta: arresto o perdita di alimentazione può lasciare generazioni miste. Dopo pubblicazione il codice verifica hash, owner/mode, ACL e sudoers; solo dopo scrive `install-complete`. L'exit trap può scrivere `install-partial`, ma un arresto forzato può non lasciarlo.

Il codice rifiuta nuove installazioni con transazioni precedenti prive di `install-complete` o `rollback-complete`, e rifiuta stati di rollback iniziati non conclusi. La ricerca M32.1 limitata per profondità e pattern non equivale a questa validazione e non autorizza una reinstallazione.

### Rollback e limiti

Il rollback richiede ID transazione, token separato vincolato a hash di transaction e manifest, controlli di path e payload, quindi ripristina/rimuove i tre target sequenzialmente secondo lo stato originario registrato. Registra status per target e marker di avvio/parziale/completamento. Un errore può lasciare uno stato misto; i tentativi ripetuti sono intenzionalmente bloccati dopo l'avvio. Questo progetto è più sicuro di un ripristino implicito, ma dipende da backup integri e autorizzazione esterna verificata.

### Runner e compatibilità

Il runner corrente importa `gateway_support` tramite il percorso Python e incorpora lo stesso pin del support. Verifica metadata e hash dei binari protetti, vincola i test/argomenti ad allowlist, usa systemd-run e log/fixture protetti, e controlla ownership/mode/ACL dei file e directory. La policy sudoers locale elenca 18 invocazioni esatte del runner, senza wildcard o shell. Questa policy limita gli argomenti, ma la sicurezza effettiva dipende anche dal runner installato, dal contenuto support, dalla configurazione sudo effettiva e dai binari autorizzati.

Il runner installato riferito non corrisponde ai bytes/pin del runner corrente. La compatibilità con l'attuale support e sudoers non è accertata: mancano i bytes storici del runner e i record di versione. Il fatto che il digest coincida con M25 rende plausibile un'origine da una generazione precedente, ma non la prova.

## 7. Analisi gateway_support.py

Il file sorgente corrente ha SHA256 `92c928ca…d514`, identico al pin `SUPPORT_SHA256` nell'installer e al valore hardcoded nel runner locale. Le sue funzioni forniscono primitive condivise di staging, manifest/evidence, attesa e terminazione controllata di unità/processi. La destinazione di installazione prevista è `/usr/local/sbin/gateway_support.py` con owner root e mode 0644, verificati a valle dall'installer.

Il collector riferisce assenza sia del target sia della copia di stage. Se il target è realmente assente, lo stato installato non soddisfa l'output richiesto dalla generazione corrente. Stage assente può invece essere normale dopo una fase installazione conclusa: lo staging è input condizionale, non output persistente. Le due assenze non sono prova della medesima causa. Senza transaction manifest completo, backup e audit non sappiamo se il support non sia mai stato pubblicato, sia stato rimosso, sia stato ricollocato, o se sia intervenuto un installer diverso.

Non ho aperto il support sulla VM né tentato accessi privilegiati. Il digest del sorgente locale prova solo i bytes locali.

## 8. Analisi collector M32.1

M32.1 include i tre target amministrativi nell'allowlist fissa. Per `/usr/local/sbin/gateway_support.py`, `PATH_ROLES` dichiara `required_installed_artifact`, base `installer output`. Se il leaf non esiste ma i componenti parent sono stati aperti e verificati, il collector emette `REQUIRED_ABSENT` e aggiunge il path agli issue; questo contribuisce a uno stato `PARTIAL`. Se manca un parent o l'accesso fallisce, registra invece `MISSING_INTERMEDIATE_DIRECTORY` o `PATH_ACCESS_ERROR`, senza confondere i casi con assenza del leaf.

Quindi **REQUIRED_ABSENT è semanticamente corretto per il target**, date le premesse del collector: il file è un output installato obbligatorio del contratto corrente. Il report M31 chiarisce la portata: è un blocker per l'installed-artifact review, non un'inferenza di assenza storica o di applicabilità di una specifica fase. Per una VM mai installata il collector non distingue intenzione/stato lifecycle; documenta lo stato presente rispetto al contratto. Questo non rende falsa l'osservazione, ma impone contestualizzazione.

I rapporti M28–M32.1 documentano un'evoluzione da hardening e copertura di inventory, a distinzione required/conditional/historical, controlli di identità, descriptor-relative no-follow, limiti di traversal/enumerazione, hashing ristretto e fsync. Nel codice M32.1 la verifica dei checksum protegge l'integrità interna dei file prodotti; non autentica il collector, l'host, l'orario o l'operatore. L'acquisizione è non atomica e il default non legge contenuti generici; zero byte hashed è coerente con la modalità metadata-only.

## 9. Analisi delle ipotesi

| Ipotesi | Elementi favorevoli | Elementi contrari / limiti | Verifiche mancanti | Conclusione provvisoria |
|---|---|---|---|---|
| **A. Installazione amministrativa incompleta** | Support target assente riferito; M27 riferisce start marker zero-byte e nessun completion marker presentato; installer pubblica tre file separatamente. | La ricerca M32.1 non ha trovato marker/manifest, ma è limitata; non sappiamo quale installer/versione fosse attivo. Sudoers/runner potrebbero essere stati collocati in altro modo. | Acquisizione completa backup root e transaction tree; esatti marker/bytes; target metadata; temp siblings; installer/stage digest; journal/audit/config-management. | Plausibile, non dimostrata. Stato installazione indeterminato. |
| **B. Generazione amministrativa precedente** | Runner riferito `5d6ee…21d5` diverge dal pin attuale `c68b…9def` e coincide con digest attribuito a M25/M27. | Nessun sorgente/versione/commit autenticato associato al digest; hash può provenire da qualsiasi origine. | Recuperare bundle/versione storica firmata, source archive, manifest e prova che bytes installati coincidano; correlare con receipt e staging. | Plausibile; non si può nominare o datare la generazione. |
| **C. Rimozione successiva di un artefatto** | Support attuale assente mentre il contratto corrente lo prevede; stage pure assente; un target può essere rimosso dopo installazione. | Nessuna prova che il support sia mai stato presente; marker di backup cercati non trovati nella profondità/pattern indicati; assenza stage non prova rimozione. | Snapshot precedenti/successivi, transaction backup con `support.existed`, audit, package/config-management log, inode/ctime affidabili e custodia. | Possibile ma non supportata da prova positiva. |
| **D. Divergenza tra installer e installazione storica** | I file locali sono untracked; installed runner hash differisce dal pin attuale; il marker vuoto riferito non corrisponde al formato corrente. | Marker può essere stato alterato o acquisito parzialmente; digest storico non identifica uno script. | Hash/copia installer storico, receipt legata a bytes, manifest di candidate e stage, record transazione completo. | Forte indicazione di mancata equivalenza al candidato corrente; causa e cronologia indeterminate. |
| **E. Requisito collector non applicabile** | Stage e receipt sono input/precondizioni condizionali, e il ciclo di vita della VM/installazione non è ricavabile dal solo leaf. | Gateway support è definito output installato richiesto dal contratto corrente; non è classificato opzionale o storico. | Definire formalmente se VM non installata/in stato preinstallazione è nell'ambito dell'acquisizione e correlare con lo stato desiderato. | Non applicabile solo se la fase osservata esclude esplicitamente l'installazione; nessuna evidenza fornita lo stabilisce. Il collector classifica correttamente il target. |
| **F. Causa non determinabile** | Manca custodia/autenticazione del bundle; transaction search limitata; nessun record VM verificabile qui; più scenari compatibili. | Nessuna evidenza che escluda in modo definitivo il recupero da acquisizioni complete future. | Bundle forense completo e autenticato, history snapshots/logs con affidabilità temporale, codice installato storico. | **Conclusione attuale più solida:** la causa non è determinabile con le evidenze disponibili. |

## 10. Test e verifiche effettuate

| Verifica | Esito |
|---|---|
| `git status --short --branch`, branch, HEAD e ultimo commit | PASS: baseline raccolta; modifiche preesistenti preservate. |
| SHA256 dei quattro file amministrativi locali | PASS: digests riportati nelle sezioni 5–7. |
| `bash -n qualification/m32-1-readonly-acquisition.sh` | PASS. |
| `bash -n qualification/install-athena-alpha-m1-admin.sh` | PASS. |
| Compilazione in memoria del Python embedded nel collector | PASS. |
| `python3 -m py_compile qualification/athena-alpha-m1-runner.py qualification/gateway_support.py tests/m32_1_readonly_acquisition_test.py` | PASS. |
| `python3 tests/m32_1_readonly_acquisition_test.py` | 48 PASS, 1 SKIP, 0 FAIL, 0 TIMEOUT su 49 test; SKIP: creazione socket Unix negata dal sandbox (`EPERM`). Tutti sintetici/non privilegiati. |
| Hash del runner installato, esistenza support, VM identity, stato transaction, permessi e ACL reali | BLOCKED: evidenza VM non accessibile/fornita in forma grezza; nessun tentativo di aggiramento. |
| Installer test harness, rollback, canary, systemd, mount, CTest privilegiati | NOT RUN: non necessari alla revisione statica e vietati dai vincoli di sessione. |
| ShellCheck | NOT RUN: non invocato. |

## 11. Verifiche bloccate

La directory evidenze M32.1 e i percorsi `/usr/local/sbin`, `/root/athena-alpha-m1-stage`, `/var/lib/athena-alpha-m1/backups` della VM non sono disponibili come acquisizione leggibile e autenticata in questo workspace. Non ho richiesto accessi né provato a superare i permessi.

Per una successiva sessione autorizzata e non mutativa, occorrono: identità VM/snapshot e boot ID; copia immutabile del bundle M32.1 e relativo SHA256SUMS; inventario no-follow completo del backup root e della directory nominata; bytes e metadata di transaction/manifest/marker/sidecar; hash e metadata dei target e stage; ACL/xattr; hash installer storico; log di audit/configuration management e journal pertinente con contesto temporale. Registrare comandi, stderr, exit status, UTC, identità operatore e trasferimento/custodia.

Comandi di sola lettura che un operatore autorizzato può valutare nel proprio runbook (non eseguiti qui): `sha256sum -c SHA256SUMS`; `find ... -xdev -maxdepth ... -printf ...` seguito da apertura no-follow e hash espliciti dei soli file autorizzati; `stat`, `getfacl`, `getfattr`, `sha256sum` sui target; acquisizione bytes con `od -An -tx1` dei marker; `journalctl --since ... --until ...` e query audit/config-management. Prima di usare `find`, il runbook deve fissare scope e limiti completi, includere file nascosti e non trattare output incompleto come assenza. Nessun comando deve scrivere, riparare o normalizzare marker.

## 12. Limiti della ricostruzione

- M25 e M27 sono citati tramite rapporti locali; non vi sono firme, acquisizioni originali con catena di custodia o identity attestations indipendenti.
- Il checksum M32.1 dichiarato verifica solo l'integrità relativa all'interno del bundle, se corretto. Il collector stesso è file locale non tracciato e la sua identità sulla VM non è legata all'output qui.
- La ricerca fino a profondità 3 e limitata a nomi specifici non prova l'assenza di transazioni, marker con altri nomi, file più profondi, link, errori di accesso o omissioni.
- SHA256 non stabilisce chi, quando, perché o attraverso quale workflow abbia installato/rimosso i bytes. Timestamp filesystem e digest isolati non sono provenienza.
- La directory walk M32.1 non è snapshot atomico; zero bytes hashed non equivale a integrità dei contenuti di tutti gli artefatti. Il suo scopo è principalmente metadata-only.
- La compatibilità runtime della generazione storica con support/sudoers attuali non è stata provata.
- Nessuna evidenza della campagna privilegiata approvata è stata esaminata; il verdetto di qualifica non cambia.

## 13. Piano di riconciliazione non esecutivo

1. **Congelare e identificare le fonti:** custodire snapshot VM e copia evidenze M32.1 in sola lettura; registrare UUID VM/snapshot, machine ID, boot ID, UTC, operatore e hash manifest. Non modificare lo stato guest.
2. **Verificare provenienza:** autenticare il collector usato e il suo SHA256 rispetto a un candidate approvato; verificare la firma/attestazione del bundle di installazione, installer e manifest candidate. La corrispondenza di hash senza firma/catena resta solo contenuto.
3. **Acquisire l'intero stato amministrativo:** tutti i tre target, stage files, path chain, mode/owner/link/ACL/xattr, directory temporanee, log, receipt e canary archive, lock e intero backup root incluse entry nascoste, contenuti marker e sidecar. Conservare bytes grezzi e checksum con output/exit status.
4. **Ricostruire le transazioni offline:** validare schema e hash di `transaction.json`, `backup-manifest.json`, marker ID e payload. Confrontare i tre target con originali/candidate registrati e ricercare eventuale installer storico corrispondente al runner hash. Non dedurre assenza da listing limitati.
5. **Correlare timeline e identità:** usare log/audit/snapshot con timestamp attendibile e stessa VM/boot ove possibile. Separare eventi attestati da metadati filesystem. Non attribuire actor o causa senza supporto.
6. **Decision gate indipendente:** security/operations review del dossier e valutazione di fixture D6, receipt, sudoers effettivo, binari, pin e prerequisiti. Prima di qualsiasi futura azione state-changing deve esistere un piano separato con autorizzazione umana, artifact root-owned/hash-pinned, allowlist argomenti precisa, backup verificato e criterio d'arresto.
7. **Esecuzione eventuale in fase separata:** nessun install/rollback/canary è autorizzato da questo rapporto. Dopo approvazione separata, eseguire unicamente l'azione deliberata e riacquisire tutto lo stato per verifica indipendente.

**Rischi:** sovrascrittura di generazione valida o evidenza; rollback parziale; incompatibilità runner/support; attivazione sudoers non valida o più ampia; perdita del fixture D6; attribuzione errata da hash uguali; snapshot/log non coerenti temporalmente.

**Punti di arresto:** identità VM ambigua; evidenza incompleta/inaccessibile; manifest/hash incoerenti; installer storico ignoto; target o ACL inattesi; marker contraddittori; backup non verificabile; concorrente amministrativo; divergenza di pin; fixture o sudoers non validati. In ciascun caso preservare e fermare il workflow.

## 14. Criteri GO/NO-GO

**GO per una fase successiva di review** solo quando esistono identità VM/snapshot e catena di custodia verificabili; acquisizione completa e internamente coerente; provenienza autenticata di collector e candidate; transazioni e target riconciliati; fixture D6 preservato; sudoers/runner/support e prerequisiti validati; revisione indipendente firmata e autorizzazione esplicita della nuova fase.

**NO-GO** se manca anche uno dei precedenti, se l'assenza del support resta inspiegata rispetto al lifecycle, se runner storico non è identificato, se marker/backup hanno incoerenze, o se occorre inferire provenienza da digest coincidenti. Un'eventuale autorizzazione a diagnosticare non equivale alla qualifica Alpha M1.

La qualifica Alpha M1 richiede inoltre il completamento della campagna Debian approvata con tutti i casi privilegiati richiesti e review delle evidenze; questa sessione non soddisfa alcuno di tali gate.

## 15. Verdetto finale motivato

**NO-GO / NOT QUALIFIED.** M32.1 riferisce una acquisizione parziale con assenza di un output amministrativo richiesto. Il runner installato riferito non coincide con il runner pinnato nel checkout corrente; il suo digest coincide con quello attribuito a M25, ma non è associato a sorgente o release autenticata. La directory di backup non è stata riconciliata integralmente e i marker/documenti storici non stabiliscono se l'installazione sia stata interrotta, appartenga a una generazione precedente o sia stata modificata successivamente. `REQUIRED_ABSENT` descrive correttamente il leaf mancante secondo il contratto attuale, senza risolverne la causa.

Rimangono aperte tutte le discrepanze sostanziali. Non dichiaro risolta l'anomalia, non dichiaro l'installer corrente compatibile con lo stato VM, non autorizzo una correzione e non dichiaro Alpha M1 qualificata.
