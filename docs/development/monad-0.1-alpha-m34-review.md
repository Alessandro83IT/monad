# Monad 0.1 Alpha M34 — revisione indipendente M33 e piano M34.1

**Esito: NO-GO / NOT QUALIFIED.** M33 ricostruisce correttamente i limiti principali: il runner VM riferito diverge dal candidato corrente, `gateway_support.py` è riferito assente, e la transazione `run-20261021-061921` non è stata riconciliata. Nessuna causa o provenienza storica è dimostrata. Questa review non ha accesso all'acquisizione grezza M32.1 né alla VM. La procedura M34.1 qui definita è solo un piano di acquisizione; non è stata eseguita e non autorizza accessi privilegiati o modifiche.

## 1. Ambito e classificazioni

Ho letto integralmente M33, i rapporti M27 e M32/M32.1 disponibili, il collector M32.1, l'installer amministrativo corrente, i test statici del collector e il runbook amministrativo M34. Ho verificato nuovamente gli hash dei quattro artefatti locali e quello del collector. L'ispezione è limitata al workspace. Il branch è `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`; lo status iniziale mostrava numerose modifiche, file non tracciati e build preesistenti. Tutti sono stati preservati. Il file di destinazione non esisteva.

Uso queste classi in senso stretto:

- **VERIFIED**: osservato direttamente nei byte/file del workspace corrente o nel codice ispezionato. Non equivale ad autenticità o provenienza.
- **INFERRED**: conclusione limitata derivata da fatti espliciti, con alternative ancora possibili.
- **UNKNOWN**: informazione non disponibile o non dimostrata.
- **BLOCKED**: verifica che richiede accesso, autorità o evidenza non disponibile in questa sessione.

Un rapporto locale non autenticato è VERIFIED come testo presente nel workspace; gli eventi VM che il rapporto descrive restano UNKNOWN se non sono riacquisiti da evidenza grezza con catena di custodia.

## 2. Valutazione indipendente di M33

La conclusione principale di M33 è sostenuta: le discrepanze sono concrete **come osservazioni riferite**, ma non identificano la causa. M33 mantiene correttamente separati digest di contenuto e provenienza, riconosce che la ricerca limitata nel backup non prova assenza, e non interpreta il marker `install-started` vuoto come prova sufficiente di una particolare sequenza di eventi. Il NO-GO è appropriato.

Precisazioni rispetto a M33:

1. `REQUIRED_ABSENT` è coerente con la tabella `PATH_ROLES` del collector per il target support, definito “installer output”. È però il risultato dell'applicazione del contratto codificato, subordinato all'affidabilità del collector e dell'output. Non prova da solo che l'installazione fosse prevista o completata nel lifecycle effettivo della VM, né autentica l'assenza osservata.
2. Un `SHA256SUMS` verificato nel bundle prova coerenza dei byte elencati rispetto a quel file checksum, non completezza del bundle, identità/versione del collector invocato, identità guest, data, operatore o trasferimento. La coppia inventario/checksum può essere stata prodotta insieme e non è una radice di fiducia indipendente.
3. M33 tratta correttamente l'assenza di stage come non diagnostica. Il file stage è input condizionale dell'installer, mentre il support installato è un output richiesto dal contratto corrente; non vanno fusi in un unico evento.
4. La ricostruzione di M33 si appoggia in parte a documenti M27 e M32 locali non autenticati. La ripetizione delle stesse osservazioni tra rapporti non è corroborazione indipendente se tutti discendono dalla stessa acquisizione.
5. L'hash del runner `5d6ee…21d5` è una corrispondenza di contenuto riferita tra M25/M27/M32.1. Chiamarla “generazione precedente” è una possibilità, non una generazione identificata. M33 in generale lo esplicita; il lessico va mantenuto sempre ipotetico.
6. Il confronto del marker storico vuoto con il writer corrente dimostra una differenza rispetto al codice locale attuale, non una violazione del protocollo storico. Versione writer, integrità e storia del marker sono UNKNOWN.

## 3. Matrice delle conclusioni e delle evidenze

| Conclusione | Stato | Evidenza disponibile | Limite / verifica ulteriore |
|---|---|---|---|
| Il checkout corrente è `main` a `abd6bac4…8939`, con modifiche preesistenti | VERIFIED | `git status`, branch e HEAD raccolti prima della scrittura | Non identifica VM o snapshot; status non attribuisce origine ai file untracked. |
| Installer locale SHA-256 `2376f43c…28dc7` | VERIFIED | Hash ricalcolato in questa sessione; file non tracciato | Nessuna prova che sia stato installato/eseguito sulla VM o sia release approvata. |
| Runner candidato locale SHA-256 `c68bca67…e19def` | VERIFIED | Hash ricalcolato; pin letto nel codice locale | Non lega il candidato ai byte installati o a una release firmata. |
| Support locale SHA-256 `92c928ca…cd514` | VERIFIED | Hash ricalcolato; pin coerente nel runner/installer locali | Non prova presenza o trasferimento sulla VM. |
| Sudoers candidato SHA-256 `263c1197…f36c18` | VERIFIED | Hash ricalcolato | Stato effettivo `/etc/sudoers.d/...` sulla VM è UNKNOWN. |
| Collector M32.1 locale SHA-256 `7d800b53…f8ee6` | VERIFIED | Hash ricalcolato in questa sessione | Non è il digest di un collector attestato per l'acquisizione riferita; va confrontato con i bytes grezzi dell'acquisizione. |
| Runner installato ha SHA-256 `5d6ee350…021d5` | UNKNOWN | Dichiarato in M32.1 e riportato in M27; M33 non dispone dei raw output | BLOCKED: riacquisire byte/hash, metadata e identità guest in sessione autorizzata. |
| `/usr/local/sbin/gateway_support.py` è assente | UNKNOWN | Status `REQUIRED_ABSENT` riferito dal bundle M32.1; M27 riporta lo stesso | BLOCKED: riacquisire target con walk no-follow e output grezzo. |
| Support stage sotto `/root/athena-alpha-m1-stage` è assente | UNKNOWN | Osservazione riferita; staging è input condizionale | BLOCKED: lettura protetta e lifecycle stage da correlare; assenza non spiega il target installato. |
| M32.1 aveva 598 entry, 1176 enumerazioni, zero byte hashed e `PARTIAL` | UNKNOWN | Numeri e risultato riportati da M33 sulla base dell'operatore | Non riaperto né ricalcolato il bundle. Zero byte hashed è coerente con modalità metadata-only, non verifica i contenuti dei target. |
| Backup `run-20261021-061921` contiene solo alcuni elementi e marker start vuoto | UNKNOWN | M27 riferisce listing parziale con `runner.absent`, `sudoers.absent` e marker vuoto | Non è un inventario completo; non stabilisce omissioni, alterazioni o stato transazione. |
| Il support non è mai stato installato / è stato rimosso / installazione interrotta | UNKNOWN | Più scenari compatibili con osservazioni riferite | Richiede backup completo, target, audit/log/snapshot e provenienza con tempi affidabili. |
| Il runner riferito è di una release storica specifica | UNKNOWN | Digest riferito uguale tra rapporti | Nessuna sorgente, release firmata, archivio o attestazione collegata a quel digest. |
| Il codice installer locale scrive marker e tre destinazioni secondo il protocollo descritto | VERIFIED | Lettura statica di `qualification/install-athena-alpha-m1-admin.sh` | Descrive solo questi byte locali; non dimostra il writer storico. |
| Alpha M1 qualificata | BLOCKED / NO-GO | La campagna Debian approvata e la review delle evidenze non sono dimostrate come completate | Nessun esito di questa review può rimuovere i gate di qualifica. |

Gli hash abbreviati nella tabella sono espansi nella sezione 4. Nessun hash VM è stato ricavato qui.

## 4. Matrice hash indipendente

| Oggetto locale | SHA-256 osservato ora | Stato | Significato |
|---|---|---|---|
| `qualification/install-athena-alpha-m1-admin.sh` | `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7` | VERIFIED | Identità dei byte locali, non dell'installer storico. |
| `qualification/athena-alpha-m1-runner.py` | `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` | VERIFIED | Corrisponde al pin corrente dichiarato dall'installer locale. |
| `qualification/gateway_support.py` | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` | VERIFIED | Corrisponde al pin corrente nel codice locale. |
| `qualification/athena-alpha-m1-sudoers` | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` | VERIFIED | Identità del candidato locale; nessuna osservazione VM attuale. |
| `qualification/m32-1-readonly-acquisition.sh` | `7d800b533b1ef8f42f3bc3e6b85d84c525742554e7ec7f51bf02c244250f8ee6` | VERIFIED | Digest del collector nel checkout corrente; identità del collector usato in M32.1 UNKNOWN. |

La tabella è una misura puntuale ripetibile del workspace in questa sessione. I file sono locali non tracciati secondo lo status iniziale. Il fatto che i pin coincidano con i candidati non autentica installer, sorgente, build, trasferimento, esecuzione o identità dell'operatore.

## 5. Analisi discrepanze amministrative

### Runner e generazioni precedenti

Il digest installato riferito `5d6ee35070954d97034de7f0323204995fe0ca86ed12fad774a3acb9ada021d5` differisce dal candidato corrente `c68bca67…e19def`. La ripetizione del vecchio digest in rapporti M25/M27/M32.1 è contenuto documentale correlato, non prova tre misurazioni indipendenti. La dicitura “generazione precedente” resta INFERRED come possibile spiegazione; versione, autore, data, compatibilità con support e sudoers e autorizzazione sono UNKNOWN. Per identificarla servono byte storici autenticati (repository/release firmata o archivio attendibile), hash indipendente e collegamento documentato a target/transazione.

### `gateway_support.py` assente

Il support è requisito installato del contratto corrente: installer lo include tra gli input pinnati e lo pubblica in `/usr/local/sbin/gateway_support.py`; il runner corrente ne dipende. Il collector classifica tale target `required_installed_artifact`. Se l'assenza è confermata su VM identificata, lo stato presente non coincide con il candidato corrente. Non si può dedurre che sia stato rimosso, mai pubblicato, o che runner e support provengano dalla stessa transazione. L'assenza del file di stage non è evidenza causale.

### Installer amministrativo

Il codice locale, letto staticamente, acquisisce stato originario dei tre target, payload/sidecar, manifest e record transazione; scrive marker `prepared` e `install-started` con ID; pubblica runner, support e sudoers con rename separati; scrive completion dopo verifiche. Le tre rename individuali non sono atomicamente un'unica transazione. Un'interruzione può lasciare target misti. Il codice corrente rifiuta transazioni incomplete prima di una nuova installazione e richiede token esplicito per rollback. Tutto ciò descrive i byte locali e non può essere applicato retroattivamente alla directory del 2026-10-21 senza identificare lo script storico.

La directory osservata M27 è stata descritta solo parzialmente; `runner.absent` e `sudoers.absent` rappresenterebbero lo stato **originario** dei target, non la loro presenza attuale. Manca l'evidenza presentata per `support.absent`/`support.existed`, payload originali, manifest, record e altri marker. Non si può concludere che tali file mancassero davvero dalla directory: possono essere stati omessi dall'acquisizione.

### Backup `run-20261021-061921`

La ricerca M32.1 riportata era limitata a profondità e pattern specifici; M27 dichiara a sua volta un listing non completo. Né un listing limitato né la mancata presentazione di un file provano assenza. Mancano almeno: inventario completo incluse entry nascoste e file speciali/link, metadata/ACL, bytes di tutti i marker, `backup-manifest.json`, `transaction.json`, payload e sidecar, eventuali temporary sibling e checksum verificabili. Non aprire, riparare o “completare” la directory. Una transazione non validabile è NO-GO.

### Collector M32.1 e provenienza degli artefatti

Il collector corrente è metadata-first, usa un'allowlist di hashing ristretta e limiti di traversal/enumerazione/output; emette `REQUIRED_ABSENT` per target required assente e costruisce `inventory.json` più `SHA256SUMS`. Il codice locale assegna al lock una specifica eccezione di hash; non va esteso il significato di zero byte hashed oltre i file effettivamente elencati. La raccolta è non atomica. Non dimostra da sola l'assenza di race, la completezza oltre i limiti dichiarati o l'autenticità dell'output.

La matrice M33 non poteva verificare l'hash del collector effettivamente invocato. L'hash locale ora misurato non colma questo vuoto. Anche una corrispondenza perfetta del digest collector prova soltanto identità di byte, salvo fonte di fiducia esterna, firma/attestazione e custodia. La provenienza dei candidati locali, delle build protette, dell'installer VM e degli output M32.1 rimane separata e non dimostrata.

## 6. Cosa è ottenibile senza privilegi e cosa no

**Disponibile senza privilegi nel workspace:** testo dei rapporti presenti, sorgenti locali e test sintetici già presenti; status/branch/HEAD; hash dei candidati accessibili; confronto statico del contratto codice corrente. Queste osservazioni non attestano la VM. Una nuova verifica di un bundle M32.1 può essere fatta senza privilegi solo se una copia grezza è resa disponibile in un percorso leggibile, identificato e preservato; il bundle non è stato trovato/fornito come evidenza grezza in questo workspace.

**Richiede futura acquisizione autorizzata:** identità host/VM/snapshot e guest/boot; target sotto `/usr/local` e `/etc`; stage sotto `/root`; backup sotto `/var/lib`; ACL/xattr e metadata protetti; marker/manifest/payload; audit/journal/configuration management; stato dei processi/unità e qualsiasi dato non leggibile dall'account ordinario. L'accesso negato resta BLOCKED: niente `sudo`, cambio account, helper privilegiati o tentativi alternativi per aggirarlo.

**Verifiche non eseguite qui:** nessun comando sulla VM; nessuna lettura privilegiata; nessun collector; nessun test/CTest, installer, canary, rollback, systemd o mount. Test non necessari alla revisione documentale e quindi NOT RUN.

## 7. Specifica di acquisizione M34.1

La finalità è acquisire e preservare, da una VM già in esecuzione e identificata, dati sufficienti per verificare le osservazioni M32.1 e riconciliare la transazione, senza modificare stato amministrativo. L'acquisizione non decide la causa e non autorizza riconciliazione.

### Fonti e scope richiesto

1. **Identità e contesto:** record host del virtualizzatore (UUID/nome VM, stato, snapshot corrente e inventario snapshot, senza transizioni), più guest hostname, machine-id, boot-id, OS/kernel e UTC. Correlare host e guest prima di qualsiasi lettura protetta. Registrare account/operatore e metodo/sessione già autorizzati.
2. **Artefatti amministrativi:** inventario no-follow completo di path chain, tipo, inode/device, mode, uid/gid, nlink, size, timestamp, ACL/xattr e mount per runner, support, sudoers, stage, directory temporanee note, lock, receipt/canary e log. Hashare solo file regolari esplicitamente approvati; preservare i byte grezzi dei tre target e del support stage se presenti. Non interpretare un errore come assenza.
3. **Backup root e transazione nominata:** enumerare senza limiti silenziosi l'intero `/var/lib/athena-alpha-m1/backups` e ogni discendente della directory `run-20261021-061921`, incluse entry nascoste, symlink, special file e nomi non attesi. Per ogni elemento raccogliere path relativo, tipo e metadata; acquisire byte/hash dei record, marker, payload e sidecar solo nel perimetro approvato. Includere manifest/transaction, tutti gli stati `.absent`/`.existed`, originali, marker, temp file e directory. Non modificare o normalizzare niente.
4. **M32.1 originale:** acquisire la directory evidenze riferita in sola lettura, l'albero completo, `inventory.json`, `SHA256SUMS`, raw stdout/stderr/exit status e qualsiasi comando/metadato di acquisizione disponibile. Conservare originale e copia di lavoro separati; ricalcolare il checksum della copia e il digest dell'albero. Se non disponibili, indicare la lacuna, non ricreare un presunto originale.
5. **Provenienza e timeline:** cercare, entro uno scope e intervallo temporale approvati, installer/runner/support staged o archiviati, manifest e log audit/configuration management/journal rilevanti. Limitare la raccolta ai dati necessari; raw log possono contenere informazioni sensibili. Timestamp filesystem da soli non sono fonte temporale affidabile.
6. **Preservazione D6:** acquisire soltanto metadata e nomi strettamente approvati per fixture e residuo D6, mantenendoli in posto. Nessun mount, lettura del payload per convenienza, cleanup, copia sopra, rename, chmod/chown o variazione di snapshot. Se la verifica di identità richiede cambiare stato, fermarsi.

### Sequenza operativa proposta (non eseguita)

1. Ottenere una decisione scritta per letture, account, host/VM/snapshot, path, intervallo temporale, trattamento dei log e destinazione dell'evidenza. Identificare un operatore e un revisore distinti. Questo piano non sostituisce l'approvazione necessaria per letture root-only.
2. Verificare che host e guest siano quelli attesi e che la VM sia già nello stato approvato. Nessun avvio/arresto, snapshot switch/restore, mount, escalation o modifica. Registrare identità e UTC prima e dopo.
3. Congelare il piano di lettura e l'eseguibile di acquisizione: review indipendente dei byte, hash/firma del collector e helper, invocazione degli stessi byte verificati (niente hash-then-run da checkout scrivibile). M32.1 corrente non è automaticamente approvato né sufficiente per questo scope ampliato. Se non si può autenticare e legare il collector eseguito all'output, fermarsi.
4. Eseguire come account ordinario per il solo scope leggibile. I percorsi inaccessibili restano BLOCKED; non rilanciare con privilegi. Per la parte protetta, richiedere una successiva autorizzazione specifica e un metodo indipendentemente revisionato. Non creare un collector privilegiato o script root.
5. Scrivere output solo in una nuova directory di acquisizione esclusiva, privata, fuori da checkout, backup originale, fixture e path amministrativi. Nessuna sovrascrittura; registrare ogni file, comando, status, stderr e limite. L'output è l'unica scrittura ammessa ed è separata dalle fonti. Se anche la creazione output non è approvata, non eseguire.
6. Al termine, chiudere la raccolta, produrre digest per-file e tree digest con walker no-follow indipendente, controllare conteggi/limiti/errori, e sigillare il verbale di custodia in un ledger separato. Non scrivere sidecar dentro le fonti. Conservare i raw bytes; analizzare copie derivate.
7. Due revisori riconciliano i risultati con la matrice hash, le regole dello schema manifest/transaction e la storia disponibile; ogni valore resta VERIFIED, INFERRED, UNKNOWN o BLOCKED con riferimento puntuale all'evidenza. Nessuna riparazione o azione amministrativa durante la review.

L'acquisizione può essere parziale e non atomica. La procedura deve dichiarare per ogni path: osservato, assente dopo verifica, non accessibile, saltato per policy, non trovato entro limite, errore o timeout. “Non elencato” non equivale ad “assente”. Un errore o superamento di un limite non deve essere convertito in successo parziale senza evidenza esplicita.

## 8. Controlli identità, integrità, provenienza e sicurezza

- **Identità:** UUID VM e snapshot dal host approvato; machine-id e boot-id dal guest; hostname/OS e orari registrati. Disaccordo o boot/snapshot variato = stop. DMI/hostname soli non sono sufficienti.
- **Integrità:** hash SHA-256 dei byte acquisiti, hash per-file, inventario canonico e tree digest indipendente; verifiche dei legami interni tra transaction, manifest, marker e payload; conteggi e output completi. Hash dei file non prova autenticità.
- **Provenienza:** fonte e custode di ciascun file, collector/helper exact-byte identity, origine dell'output, account, orari e trasferimento documentati. Cercare firme o attestazioni indipendenti per release/install bundle. Digest coincidente senza radice fidata resta identità contenuto, non autore/data/approvazione.
- **Sicurezza:** sola lettura delle fonti; no-follow su ogni componente, controllo tipi/link, ACL/xattr e mount; limiti espliciti ma nessuna omissione silenziosa; output in directory nuova e privata; log trattati come potenzialmente sensibili; nessuna esecuzione di runner, unità, installer, test o helper con privilegi.
- **Concorrenza e durata:** acquisizione live non atomica. Registrare inizio/fine e ricontrollare identità; se lo stato cambia durante la raccolta, correlare i tempi o classificare l'insieme come non coerente. Non sospendere processi o congelare filesystem.

## 9. Criteri di arresto

Fermare l'acquisizione, preservare output parziale e classificare BLOCKED in caso di identità VM/snapshot/boot incerta o mutata; permesso negato; symlink, hardlink o tipo inatteso; ACL/owner/mode/mount inattesi; mismatch hash, manifest o marker; directory/log non completamente enumerabili; limiti/timeout/output troncato; file cambiato durante lettura; collector non autenticato o diverso dai byte approvati; destinazione output preesistente/non sicura; concorrente amministrativo; anomalia fixture/D6; o qualsiasi necessità di elevare privilegi o alterare lo stato.

Non correggere marker, non ripetere letture fallite aumentando privilegi, non cancellare output parziale, non riavviare la VM e non usare l'installer/rollback per “rendere coerente” lo stato.

## 10. Criteri GO / NO-GO per riconciliazione amministrativa successiva

**GO soltanto per sottoporre una proposta di riconciliazione a nuova approvazione** quando tutti i punti seguenti sono soddisfatti e revisionati indipendentemente:

- identità VM/snapshot/boot e catena di custodia verificabili;
- bundle M32.1 originale preservato e ricalcolato, collector esatto identificato o esplicita dichiarazione che non è possibile autenticarlo;
- inventario completo e coerente di backup root, transazione, tutti i tre target, stage e marker, con errori/omissioni risolti;
- schema/byte di `transaction.json` e `backup-manifest.json` validi secondo il writer storico identificato, marker coerenti e payload verificati;
- provenienza del runner storico attribuita a una release o dichiarata irrisolvibile senza inferenze; relazione target/support/sudoers spiegata da evidenza positiva;
- nessun dato conflittuale non risolto, nessuna alterazione concorrente, D6 preservato e stato dei log/fixture/receipt documentato;
- decisione separata di Security e Operations e autorizzazione umana che nomini esattamente la futura operazione, artefatti, VM, backup e limiti.

Questo GO consente al massimo di preparare/revisionare una proposta di azione amministrativa, non di eseguirla. Ogni lettura privilegiata e ciascuna azione state-changing richiedono autorizzazioni proprie. Installazione, rollback, staging, canary e campagna privilegiata non sono autorizzati da M34 o M34.1.

**NO-GO** se anche un solo requisito manca; in particolare se il backup è incompleto/illeggibile, marker o record contraddittori, runner non attribuibile, support assente senza spiegazione supportata, identità/provenienza non dimostrata, o l'argomento a favore dipende da hash isolati o timestamp filesystem. NO-GO resta la decisione corrente.

## 11. Riepilogo verifiche e raccomandazione

| Verifica in questa sessione | Esito |
|---|---|
| Stato Git, branch e HEAD prima della scrittura | PASS — baseline registrata; modifiche preesistenti preservate. |
| Lettura integrale rapporto M33 | PASS |
| Confronto statico di installer, collector M32.1, rapporti M27/M32 e runbook M34 disponibile | PASS per il contenuto locale; provenienza esterna non attestata. |
| Ricalcolo hash installer, runner, support, sudoers e collector M32.1 locali | PASS — valori in §4. |
| Ispezione del bundle M32.1 grezzo, backup o target VM | BLOCKED — non disponibili in forma acquisibile/authenticata. |
| Test/CTest o invocazione collector | NOT RUN — non necessari; nessuna esecuzione amministrativa. |
| Modifica di VM, snapshot, D6, collector, installer, runner, sudoers o pin | NOT RUN |

**Raccomandazione finale: NO-GO / NOT QUALIFIED.** M33 è sostanzialmente corretto nel giudizio prudente. Le informazioni presenti non consentono di provare quale installer abbia agito, se o quando il support sia stato pubblicato, quale sia la generazione del runner o quale fosse lo stato completo della transazione. Preparare M34.1 come acquisizione autorizzata e read-only è il passo successivo appropriato; solo una review indipendente del dossier completo potrà valutare se proporre, in una fase distinta, una riconciliazione amministrativa. Alpha M1 rimane NON QUALIFICATA.
