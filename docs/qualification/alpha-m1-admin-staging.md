# Athena Alpha M1 — revisione e installazione amministrativa

Stato: **preparazione locale pronta per revisione**. In questa revisione non sono stati modificati `/root/athena-alpha-m1-stage`, installati runner o sudoers, invocati comandi privilegiati o eseguiti test Alpha M1 privilegiati. Alpha M1 resta non qualificata fino all'esecuzione dei 18 casi nella VM prevista.

## Componenti e hash correnti del repository

| File amministrativo | SHA-256 |
|---|---|
| `qualification/athena-alpha-m1-runner.py` | `2290b78c55c72489c0e7dc3bb1c1fedbc573e6bc605e690b6afd3a8ab326b24c` |
| `qualification/gateway_support.py` | `86be38660154161b6f71063567241fed3fbe1ef270d04a73b7c00a69126a9d28` |
| `qualification/athena-alpha-m1-systemd-canary.py` | `e926d2801e2923a38598f23f2a3d94d35303e2e68ee9e833c7e13b0e2a5c4592` |
| `qualification/athena-alpha-m1-sudoers` | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` |
| `qualification/install-athena-alpha-m1-admin.sh` | `55f13e8d1d91e149b6f24a8257fa48339f2b48e2a1b619f5ae0a5b1053d463a8` |
| `qualification/run-alpha-m1-privileged.py` | `20a87b8fc6f2f8cf55bb153b4fcc9362f937a97857e894866e0d05963d8ea83e` |

I sei digest elencati identificano i file nel checkout osservato. Il pin canary ancora incorporato nell'installer è `0c2567320d3f5c461d0c9a4a19beaf62b83615c511c28e699810bbdfa49f3107`, diverso dal sorgente corrente `e926d2801e2923a38598f23f2a3d94d35303e2e68ee9e833c7e13b0e2a5c4592`; la divergenza è intenzionale e resta **PROMOTION PENDING**. Il pin non è stato aggiornato automaticamente. Gli altri tre pin incorporati (runner, support, sudoers draft) corrispondono ai file osservati. Non eseguire staging o `--record` per questo candidato finché il pin canary non è stato promosso con review esplicita e test non privilegiati superati. Installer e orchestratore non sono auto-pinned: la loro identità deve essere inclusa nel manifest di trasferimento approvato e ricontrollata separatamente nella VM. Ricalcolare tutti i digest immediatamente prima dello staging; qualsiasi differenza interrompe la procedura.

La verifica della sessione 31 distingue il build protetto esistente dal build Debug isolato aggiornato: `/opt/athena-qualification/build` ha 140 test, di cui 18 privilegiati, e i suoi sette artefatti autenticati corrispondono ai pin correnti dell'installer. Il build isolato aggiornato `/tmp/monad-s32-verification-final4-20261021` ha 147 test, sempre con gli stessi 18 privilegiati, e sette artefatti con digest diversi; include sette test non privilegiati aggiuntivi rispetto al build protetto: tre cleanup fixture e quattro test Alpha M1 static/canary/orchestrator. Il vecchio `build-monad-debug` nel checkout è obsoleto per questa verifica e contiene 143 test. Nessun build isolato è promosso per la campagna. La corrispondenza degli hash del build protetto prova l'identità con i pin attuali, non la provenienza aggiornata rispetto al checkout. Prima di qualificare i nuovi artefatti, ricostruire e attestare il candidato nella VM e sincronizzare i pin solo tramite promozione revisionata; non rigenerarli automaticamente.

## Correzione sandbox: `openat2` e bit SUID/SGID

La riproduzione sulla VM mostra che `RestrictSUIDSGID=yes`, da solo, trasforma `openat2()` in `ENOSYS`; la chiamata funziona fuori da quel filtro e nel sandbox parziale. Il sorgente upstream systemd v257 conferma il meccanismo: seccomp non può ispezionare i flag dentro la struttura indiretta `struct open_how`, quindi il filtro installato per `RestrictSUIDSGID` nega completamente `openat2` con `ENOSYS` e intende forzare l'uso di `open()`/`openat()`. La configurazione del runner e il canary non impostano più `RestrictSUIDSGID`; non è stato aggiunto alcun fallback a `openat` nel codice del package manager.

La rimozione consente al processo di creare file con bit SUID/SGID nelle sue directory scrivibili. Le mitigazioni mantenute e controllate sono `NoNewPrivileges=yes`, `PrivateMounts=yes`, un capability bounding set limitato a `CAP_SYS_ADMIN CAP_CHOWN`, fixture ext4 root-owned `0700` accessibile solo a root e controllo che la fixture sia vuota al termine. Con `NoNewPrivileges`, l'esecuzione non può acquisire privilegi via setuid/setgid; inoltre il canary ora deve verificare l'opzione `nosuid` nei mount namespace per entrambi i percorsi scrivibili (`/tmp` e fixture ext4). Un residuo con bit speciali potrebbe comunque essere persistito nella fixture: il runner lo conserva come evidenza e fallisce sul controllo di directory non vuota. Il canary live è obbligatorio e deve passare prima dell'installazione; se non osserva `nosuid`, fermarsi senza installare il runner.

Il riferimento al sorgente di systemd v257 è [seccomp-util.c](https://raw.githubusercontent.com/systemd/systemd/v257/src/shared/seccomp-util.c), nella regola `seccomp_restrict_sxid()` e nell'implementazione `seccomp_restrict_suid_sgid()`. Il patchset Debian esatto e il comportamento effettivo della VM vanno comunque confermati dal canary aggiornato durante lo staging.

La fixture dedicata per i casi ext4 è `/var/lib/athena-alpha-m1/ext4-fixture`. L'installer non formatta né monta dispositivi: rifiuta se `/var/lib/athena-alpha-m1` e la fixture non risultano su ext4, o se la fixture non è una directory root-owned `0700`, ACL-free e vuota. Runner e canary usano lo stesso percorso come unica eccezione di scrittura persistente del test.

## Installazione manuale nella VM

Eseguire i passaggi seguenti come amministratore nella VM di qualificazione, dopo aver verificato identità, snapshot/disposable state e corrispondenza del build autenticato. Non trasferire file da uno staging precedente. Sostituire `/path/to/reviewed` con la directory dei file revisionati.

Prima di preparare lo staging, eseguire il collector read-only [alpha-m1-vm-preflight.sh](alpha-m1-vm-preflight.sh) da una shell interattiva sulla VM e conservare l'output fuori dal checkout. Il collector non esegue test né canary e il suo exit code 0 indica soltanto che la raccolta è terminata: ogni dato `NOT VERIFIED`, hash o inventario inatteso, unità fallita, mount errato o fixture non vuota è una condizione di stop. Verificare separatamente snapshot/disposable state e identità VM. Il collector eseguito nel container di sviluppo non costituisce preflight VM.

La revisione statica del timeout ora conferma criteri terminali dell'unità/cgroup prima della pulizia, vincola il cgroup al path atteso e propaga `KeyboardInterrupt` come esito interrotto senza receipt. Security ha chiuso questo rilievo e ha revisionato la serializzazione concorrente dei publisher tramite lock flock a tempo limitato. Core riporta 16/16 test canary/child e 7/7 source-closure test; ciò non prova il comportamento systemd/cgroup runtime, che resta da verificare nella VM.

Lo stage deve essere vuoto, root-owned `0700` e ACL-free. Copiare i quattro file autenticati, quindi verificare i digest prima di proseguire:

```sh
install -d -o root -g root -m 0700 /root/athena-alpha-m1-stage
install -o root -g root -m 0600 /path/to/reviewed/qualification/athena-alpha-m1-runner.py /root/athena-alpha-m1-stage/athena-alpha-m1-runner.py
install -o root -g root -m 0600 /path/to/reviewed/qualification/gateway_support.py /root/athena-alpha-m1-stage/gateway_support.py
install -o root -g root -m 0600 /path/to/reviewed/qualification/athena-alpha-m1-sudoers /root/athena-alpha-m1-stage/athena-alpha-m1-sudoers
install -o root -g root -m 0600 /path/to/reviewed/qualification/athena-alpha-m1-systemd-canary.py /root/athena-alpha-m1-stage/athena-alpha-m1-systemd-canary.py
sha256sum /root/athena-alpha-m1-stage/athena-alpha-m1-runner.py /root/athena-alpha-m1-stage/athena-alpha-m1-sudoers /root/athena-alpha-m1-stage/athena-alpha-m1-systemd-canary.py
```

Se `/root/athena-alpha-m1-stage` esiste già, controllarne owner, mode, ACL e contenuto prima di riutilizzarlo; non sovrascrivere file inattesi.

1. Verificare gli hash coerenti del report aggiornato con `sha256sum` e controllare che `/root/athena-alpha-m1-stage` sia una directory root-owned `0700`, ACL-free e vuota. Copiare lì runner, support module, sudoers e canary con owner root e mode `0600`.
2. Verificare `/var/lib/athena-alpha-m1`, `/var/lib/athena-alpha-m1/backups` e `/var/log/athena-alpha-m1` come directory root-owned `0700`, senza ACL estese. Confermare con `findmnt -T /var/lib/athena-alpha-m1 -o SOURCE,FSTYPE,OPTIONS` che il filesystem sia ext4. Non creare o cambiare filesystem.
3. Preparare/verificare la fixture e convalidare il canary:

   ```sh
   /path/to/reviewed/install-athena-alpha-m1-admin.sh prepare-fixture
   stat -c '%F %a %u:%g %n' /var/lib/athena-alpha-m1/ext4-fixture
   getfacl -cp /var/lib/athena-alpha-m1/ext4-fixture
   findmnt -n -T /var/lib/athena-alpha-m1/ext4-fixture -o TARGET,SOURCE,FSTYPE,OPTIONS
   /usr/bin/python3 /root/athena-alpha-m1-stage/athena-alpha-m1-systemd-canary.py --record
   stat -c '%F %a %u:%g %n' /var/lib/athena-alpha-m1/systemd-canary.json
   getfacl -cp /var/lib/athena-alpha-m1/systemd-canary.json
   ```

   Procedere solo con canary exit 0 e receipt canonica root-owned `0600`, senza ACL estese. La receipt vincola hash di canary e runner, boot ID e versione systemd; scade dopo 24 ore. Il canary conserva il precedente receipt canonico con un hard link dal nome archivio univoco nella stessa directory soltanto dopo PASS e conferma di terminazione; poi pubblica atomicamente il nuovo file al path canonico. Un lock `.systemd-canary.receipt.lock` root-owned `0600`, nlink 1 e ACL-free serializza i publisher con attesa massima 30 secondi; non rimuoverlo tra esecuzioni. Non rimuovere né sovrascrivere manualmente il canonico o gli archivi. Per esiti timed-out, interrupted o con terminazione non confermata, il receipt precedente resta invariato e non si pubblica un nuovo receipt. La regressione SIGINT è stata corretta e revisionata staticamente; il comportamento runtime resta da provare nella VM. Preservare marker e ogni eventuale hard link archivio creato prima di un errore di pubblicazione.

4. Prima dell'installazione, verificare metadati e hash degli artifact in `/opt/athena-qualification/build`; `visudo` deve essere disponibile. Verificare che l'installer mostri hash concordanti e poi scegliere un backup path univoco:

   ```sh
   /path/to/reviewed/install-athena-alpha-m1-admin.sh install \
     /var/lib/athena-alpha-m1/backups/run-YYYYMMDD-HHMMSS
   /usr/sbin/visudo -c
   ```

   L'installer valida sorgenti, receipt, build, ACL e hash; salva prima lo stato precedente, installa file temporanei e rinomina runner, support module e sudoers. Gli swap sono atomici per file, non come transazione unica: un'interruzione tra rename richiede rollback/manual recovery. La regola sudoers risultante consente solo i 18 argomenti elencati, senza shell o wildcard. Non autorizza il canary.

## Esecuzione e raccolta evidenze

Dalla directory del repository, lanciare un solo comando non privilegiato:

```sh
python3 qualification/run-alpha-m1-privileged.py --build-dir /opt/athena-qualification/build
```

Lo script confronta prima l'inventario CTest etichettato `privileged` con la lista esatta dei 18 casi; in caso di differenza non invoca sudo e registra tutti i casi come `BLOCKED`. Se non riesce a ottenere/parlare l'inventario, registra i casi come `NOT_RUN`. In entrambi i casi scrive il preflight log, `results.json` e `results.tsv`. Dopo il controllo esegue in sequenza un `sudo -n /usr/local/sbin/athena-alpha-m1-runner CASE` per ciascun caso e registra log individuali, `results.json`, `results.tsv` e durata/exit code sotto `qualification/alpha-m1-privileged-results/<UTC>/`. `runner_started` e il marker finale del runner distinguono un effettivo risultato del binario da errori sudo/avvio. Non usare `ctest` per l'esecuzione privilegiata: solo l'orchestratore passa dalla allowlist sudoers.

Conservare integralmente la directory risultati e i log root in `/var/log/athena-alpha-m1`. Il runner scrive JSON schema 3, output limitato, digest stream/output, manifest di staging e sidecar d'integrità con hash report/output. Verificare i sidecar con il helper revisionato; sono alterabili da root sullo stesso host e non costituiscono audit immutabile. Accettare un caso solo se exit code 0 e risultato `PASS`. Le classificazioni includono `TEST_FAIL`, `INFRA_ERROR`, `STOP_REQUESTED`, `TIMEOUT`, `TERMINATION_UNCERTAIN`, `EVIDENCE_ERROR`, `BLOCKED` e `NOT_RUN`. La fixture viene verificata vuota solo dopo la conferma di terminazione di unità e cgroup. Non dichiarare Alpha M1 superata senza evidenze originali di tutti i 18 test.

## Rollback e recupero parziale

Rollback ordinario, usando esattamente il backup path dell'installazione:

```sh
/path/to/reviewed/install-athena-alpha-m1-admin.sh rollback \
  /var/lib/athena-alpha-m1/backups/run-YYYYMMDD-HHMMSS
/usr/sbin/visudo -c
```

Il backup è conservato. Prima di toccare qualsiasi destinazione, il rollback verifica il manifest aggregato `backup-manifest.json`, l'inventario esatto dei payload/marker, tipo, uid/gid, mode e SHA-256 di tutti i file di backup, e prevalida le destinazioni. I sidecar `.mode`, `.sha256` e `.owner-group` conservano i metadati del file originale; il ripristino conserva uid/gid/mode. Poi ripristina runner, support e sudoers con rename atomico per file e valida sudoers. Non è una transazione atomica unica: un errore I/O durante i singoli ripristini può lasciare uno stato parziale. È ripetibile quando le evidenze restano coerenti. Se segnala `PARTIAL ROLLBACK`, interrompere la qualificazione e conservare il backup. Esaminare manifest, marker `.existed`/`.absent`, sidecar, status, owner/mode/ACL e destinazioni. Non rimuovere symlink, file inattesi, fixture o mount per automatizzare il recupero. Ripetere `visudo -c` e controllare tutti e tre i target.

In caso di errore durante l'installazione, l'installer attiva un rollback best-effort dopo il marker `install-started`. Un errore prima del marker non ha ancora sostituito i target; un errore dopo il marker può lasciare un rollback parziale. In entrambi i casi seguire il messaggio, mantenere il backup path e verificare manualmente entrambi i target prima di riprovare. Nessuna pulizia del backup è automatica.

## Verifiche ancora necessarie

Questa preparazione non attesta il comportamento runtime del canary/systemd, il flag `nosuid` sui mount scrivibili della VM, la disponibilità di capability mount né i test privilegiati. Il canary aggiornato deve essere eseguito nella VM e la sua receipt verificata prima di installare. I valori sopra identificano i file revisionati nel repository.

## Riconciliazione amministrativa — 2026-10-21

Le osservazioni locali provengono da un container (`systemd-detect-virt=container-other`, PID 1 `codex`, UID 1000), non dalla VM Debian target. La sua `/etc/os-release` dice Debian 13, ma non prova l'identità della VM. In questo namespace i path del runner e di sudoers risultano presenti con UID/GID mappati `65534:65534`; il contenuto di sudoers e la fixture installata non sono leggibili. Non dichiarare né assenza né validità dei file installati sulla VM da questi dati. La verifica effettiva richiede lettura amministrativa circoscritta sull'host target.

Codex CLI è disponibile qui come `codex-cli 0.163.0`; `.codex/config.toml` abilita gli agenti e i quattro profili TOML sono presenti. Questo conferma configurazione locale e versione del binario, ma non il caricamento runtime sulla VM. Verificare lì con `codex --version` e `codex --help`; non avviare una sessione agent durante il preflight.

`/opt/athena-qualification/build` è presente in sola lettura, modalità 0550, UID/GID mappati 65534:65534. L'inventario `ctest --show-only=json-v1 -L privileged` restituisce 18 casi e corrisponde all'allowlist dell'orchestratore, con gli eseguibili presenti. Non è una prova di freschezza o provenienza del build. `ctest -N` ha tentato di scrivere `Testing/Temporary/LastTest.log` e ha fallito; per l'inventario usare esclusivamente `--show-only=json-v1`. Nessun caso è stato eseguito.

Il residuo `/var/tmp/athena-d67-ext4` è visibile solo come directory 0700 con UID/GID mappati 65534:65534; l'elenco dei contenuti è negato. La fixture `/var/lib/athena-alpha-m1/ext4-fixture` non è accessibile. Contenuto, mount e conservazione del residuo D6 sono quindi **NOT VERIFIED**. Non cancellare né modificare entrambi i percorsi.

Il preflight locale non distruttivo è `python3 scripts/alpha_m1_admin_preflight.py`. Raccoglie hash, pin installer, ambiente, CLI/configurazione, inventario CTest, metadati amministrativi e fixture senza eseguire test o elevare privilegi. Ogni `NOT VERIFIED`, `FAIL` o `BLOCKED` impedisce la promozione.

### Lettura amministrativa circoscritta richiesta sulla VM

Un amministratore deve eseguire sulla VM e conservare l'output di questi soli controlli di lettura prima di approvare qualsiasi mutazione:

```sh
id
cat /etc/os-release
systemd-detect-virt
cat /proc/sys/kernel/random/boot_id
systemctl is-system-running
codex --version
stat -c '%F %a %u:%g %n' /usr/local/sbin/athena-alpha-m1-runner /usr/local/sbin/gateway_support.py /etc/sudoers.d/athena-alpha-m1 /opt/athena-qualification/build /var/lib/athena-alpha-m1 /var/lib/athena-alpha-m1/ext4-fixture /var/tmp/athena-d67-ext4
sha256sum /usr/local/sbin/athena-alpha-m1-runner /usr/local/sbin/gateway_support.py /etc/sudoers.d/athena-alpha-m1
getfacl -cp /usr/local/sbin/athena-alpha-m1-runner /usr/local/sbin/gateway_support.py /etc/sudoers.d/athena-alpha-m1 /var/lib/athena-alpha-m1/ext4-fixture
findmnt -T /opt/athena-qualification/build -o TARGET,SOURCE,FSTYPE,OPTIONS
findmnt -T /var/lib/athena-alpha-m1/ext4-fixture -o TARGET,SOURCE,FSTYPE,OPTIONS
find /var/lib/athena-alpha-m1/ext4-fixture /var/tmp/athena-d67-ext4 -mindepth 1 -maxdepth 2 -printf '%y %m %u:%g %p\n'
/usr/sbin/visudo -c
ctest --test-dir /opt/athena-qualification/build --show-only=json-v1 -L privileged
```

Se la directory D6 non è accessibile all'utente amministrativo, raccoglierne l'inventario con il solo comando `find` come root, senza `rm`, `rmdir`, mount o scritture. Confermare separatamente snapshot/disposable state e identità della VM. Il risultato deve essere archiviato fuori dal checkout con data UTC e boot ID.

### Limite transazionale e condizioni di arresto

L'installer sostituisce runner, support e sudoers con tre rename atomici distinti; il rollback ripristina i tre file uno alla volta. SIGKILL, perdita di alimentazione o errore I/O tra rename può lasciare generazioni miste; il trap best-effort non sopravvive a SIGKILL/power loss. Dopo marker `install-started` e interruzione, non rilanciare `install` né rollback automaticamente. Fermare la campagna, preservare backup, marker, receipt, log e fixture; leggere hash/metadati di tutti i target e marker, eseguire `visudo -c`, quindi l'amministratore sceglie esplicitamente il recupero dopo aver validato l'intero manifest del backup.

Condizioni di stop: identità/snapshot VM non provati; qualsiasi hash divergente; build non autenticato o inventario non esattamente 18; path/ACL/owner/mode imprevisti; canary o receipt non validi; fixture non ext4 o non vuota; visudo non disponibile/non valido; presenza di marker di installazione/rollback parziale; esito mancante o diverso da PASS. In tutti i casi mantenere ogni fixture, residuo, backup e log.

### Comandi di promozione e rollback da sottoporre ad approvazione

Questi comandi mutano lo stato amministrativo e **non sono stati eseguiti**. L'approvazione deve nominare hash esatti, VM/boot ID e backup path univoco. Dopo i controlli di lettura, staging protetto dei quattro file autenticati, `prepare-fixture` e canary `--record`, il comando di promozione è:

```sh
/path/to/reviewed/install-athena-alpha-m1-admin.sh install /var/lib/athena-alpha-m1/backups/run-YYYYMMDD-HHMMSS
/usr/sbin/visudo -c
```

Dopo approvazione separata dell'esecuzione della campagna, l'orchestratore non privilegiato esegue i soli 18 casi. Un rollback richiede approvazione esplicita separata e l'esatto backup validato:

```sh
/path/to/reviewed/install-athena-alpha-m1-admin.sh rollback /var/lib/athena-alpha-m1/backups/run-YYYYMMDD-HHMMSS
/usr/sbin/visudo -c
```

Verificare dopo ogni transizione SHA-256, owner/group/mode/ACL dei tre target, `visudo -c`, stato unità/cgroup e fixture. Mantenere il backup anche dopo rollback. Questa preparazione non approva automaticamente gateway o campagna.

## Manifest di riconciliazione dei build — sessione 31

Il generatore [alpha_m1_build_reconcile.py](../../qualification/alpha_m1_build_reconcile.py) raccoglie in sola lettura gli hash dei sette artefatti autenticati dall'installer, gli hash runner/support/canary/sudoers rispetto ai pin correnti, cache CMake, inventario CTest completo e inventario privileged ordinato. Usa esclusivamente `ctest --show-only=json-v1`; non esegue casi, canary, sudo o operazioni amministrative. Crea il JSON con `O_EXCL` e rifiuta di sovrascrivere un file esistente. Hasha gli input del checkout con `O_NOFOLLOW` per ogni componente del percorso e rifiuta symlink o file input non regolari. Gli output JSON del generatore e altri file non sorgente sono elencati tra gli untracked esclusi, non vengono incorporati nella fingerprint sorgente.

Esempio dal checkout, con un nome nuovo per ogni acquisizione:

```sh
python3 qualification/alpha_m1_build_reconcile.py \
  --protected-build /opt/athena-qualification/build \
  --isolated-build /tmp/monad-s32-verification-final4-20261021 \
  --output qualification/alpha-m1-build-reconciliation-<UTC-univoco>.json
```

Il profilo `protected` descrive il build immutabile già presente in `/opt`; `isolated_updated` descrive il build isolato del checkout. Non scambiare i ruoli né usare automaticamente il secondo nella campagna. Il record della working tree contiene HEAD, stato Git, hash dei file di input tracciati e degli input sorgente non tracciati nei percorsi dichiarati; elenca gli untracked esclusi. Questa impronta aiuta la provenienza, ma non dimostra che un binario sia stato compilato da quella snapshot. Il manifest espone compiler, build type, source directory, hash degli artifact e inventory per consentire la revisione incrociata.

Il candidato è distinto da entrambi: aggiungerlo solo dopo approvazione esplicita e registrare il riferimento approvativo tramite `--candidate-build` e `--approval-reference`. Il testo del riferimento è un'attestazione dell'operatore, non viene verificato dal generatore. Nessun percorso o digest viene promosso o riscritto dal comando. Registrare esternamente l'hash SHA-256 del manifest JSON; fermarsi su pin discordanti, inventory privileged diversa dall'orchestratore, artifact mancanti, configurazione inattesa o provenienza non verificabile. L'acquisizione finale [alpha-m1-build-reconciliation-20261021T092750Z.json](../../qualification/alpha-m1-build-reconciliation-20261021T092750Z.json), SHA-256 `9d81c2e7ccfe02472b855e653a74390ede05d10a63a380cda9f7240762018547`, osserva `/opt/athena-qualification/build` con 140 test (18 privileged), hash dei sette artifact corrispondenti ai pin installer correnti; il build Debug isolato aggiornato `/tmp/monad-s32-verification-final4-20261021` ha 147 test (18 privileged esatti) e artifact non corrispondenti ai pin. Il vecchio `build-monad-debug` nel checkout ha 143 test e non è il build aggiornato usato qui. I manifest intermedi sono conservati; in particolare `alpha-m1-build-reconciliation-20261021T153000Z.json` riporta `generated_utc=2026-10-21T09:15:30Z`, quindi il timestamp del nome è errato e quel record è superseduto. Questi valori descrivono i percorsi e le snapshot osservate, non attestano il build isolated come candidato approvato o qualificato per la campagna.

### Promozione revisionata del pin canary e receipt fresca

Il canary corrente usa il path canonico già letto dall'installer, `/var/lib/athena-alpha-m1/systemd-canary.json`. Il suo sorgente finale congelato da Core ha SHA-256 `e926d2801e2923a38598f23f2a3d94d35303e2e68ee9e833c7e13b0e2a5c4592`; il pin `CANARY_SHA256` nell'installer resta `0c2567320d3f5c461d0c9a4a19beaf62b83615c511c28e699810bbdfa49f3107`. La promozione del pin è **PENDING**: non eseguire staging, canary `--record` o installazione finché la review approvata non riconcilia questi digest. La review deve controllare diff e regressioni non privilegiate di conservazione, poi cambiare il pin nell'installer con una patch esplicita sottoposta a review. Non aggiornare pin con il generatore, comandi automatici o valori copiati senza review. Se cambiano runner/support, riconciliare anche i loro pin e il candidato build.

Contratto della receipt revisionato staticamente da Security: il canary verifica il path canonico senza modificarlo prima della prova. Scrive e sincronizza una receipt temporanea root-owned `0600`, ma la pubblica solo dopo `systemd-run` exit 0 e conferma bounded della terminazione dell'unità/cgroup. Se una receipt canonica esiste, ne crea un hard link con nome univoco `.systemd-canary-archive.<nonce>.json` nella stessa directory, confronta inode e byte/hash, sincronizza la directory, quindi pubblica atomicamente la nuova receipt al path canonico e sincronizza di nuovo la directory. Un lock flock root-owned `0600`, nlink 1 e ACL-free, acquisito non-bloccante con deadline monotonic di 30 secondi, resta tenuto durante ricontrollo, archiviazione e pubblicazione; così due publisher non possono perdere una receipt intermedia. Se il lock è unsafe o il timeout scade, la prova fallisce chiusa senza modificare le receipts. L'installer continua a validare il path canonico; non serve un'interfaccia receipt-path diversa. Security ha revisionato il contratto di archiviazione e lock; il runtime resta da qualificare.

Per un timeout o una terminazione esplicitamente non confermata, `record_success` non viene chiamato: canonico e archivi esistenti restano invariati. La regressione SIGINT è chiusa nella versione finale e il publisher concorrente è serializzato dal lock. Se il passo di archiviazione o pubblicazione restituisce un errore, fermarsi e preservare il canonico, ogni archivio creato e ogni receipt temporanea; una failure dopo la creazione del link può lasciare un archivio aggiuntivo, mentre una failure dopo il rename atomico può aver già sostituito il canonico. In entrambi i casi il contenuto precedente è mantenuto dall'archivio hard link quando esiste; non rilanciare canary, rinominare, rimuovere o ripristinare file automaticamente. Un amministratore deve ispezionare i due nomi, hash, inode, owner, mode e ACL e decidere il recupero esplicitamente.

Dopo pin promotion revisionata, verifica non privilegiata e approvazione amministrativa, identificare VM/snapshot e boot ID; registrare l'hash e metadati del receipt canonico esistente, se presente. Eseguire una sola volta il comando approvato `/usr/bin/python3 /root/athena-alpha-m1-stage/athena-alpha-m1-systemd-canary.py --record`. Con exit 0, verificare il nuovo receipt canonico root-owned `0600`, senza ACL, valido per hash canary/runner promossi, boot ID e versione systemd correnti e non scaduto. Se esisteva un canonico precedente, verificare che uno e un solo nuovo nome archivio punti allo stesso inode e mantenga esattamente i suoi byte/hash; conservare tutti gli archivi storici e il nuovo canonico. La receipt non autorizza da sola installazione o i 18 casi: restano necessarie le approvazioni amministrative separate e tutti gli altri stop condition del runbook.
