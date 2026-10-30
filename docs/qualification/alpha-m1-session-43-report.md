# Alpha M1 — Session 43: VM preflight reconciliation & diagnostic acquisition readiness

**Decision: BLOCKED.** I controlli locali di integrità e le quattro revisioni tecniche non hanno rilevato una discrepanza nei byte esaminati. Non è però possibile dichiarare **READY FOR HUMAN APPROVAL**: non sono state ottenute da questo container prove dirette host/guest, il binding VM↔snapshot↔guest, né una destinazione guest approvata. Inoltre i documenti esistenti non contengono un comando `--execute` completo e revisionato per il loader Sessione 42. Non è stata concessa né inferita autorizzazione all'acquisizione.

**Ambito:** solo verifiche read-only e analisi statica. Runner, collector, test, CTest e procedure amministrative non sono stati eseguiti. Nessuna modifica ai 149 file storici, agli artefatti Sessione 34B, al runner, al collector, a servizi, mount o stato VM.

## Baseline e preservazione

- Repository `/home/alessandro/Projects/monad`, branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`.
- Working tree già sporco al controllo iniziale, con modifiche tracked, documentazione/script/build directory e altri artefatti non tracked. Il report è l'unico nuovo file creato in questa sessione. Tutto il lavoro preesistente è stato lasciato intatto.
- Container osservato: uid 1000, virtualizzazione `container-other`; `VBoxManage` non disponibile. Questi dati non descrivono l'host o il guest target.
- Le osservazioni fornite dall'operatore (host `gentoo-msi`, VM `Athena-Alpha-M1`, UUID `e475ea91-72e9-4157-9ee6-583e9ae0462f`, snapshot `Alpha-M1-Before-Privileged-Tests-20261020` / UUID `12e7d712-0154-4127-9fb2-8dd848242877`, guest Debian con machine-id e boot-id riportati, ext4 e destinazione candidata) restano **riferite dall'operatore**. Non sono state osservate o autenticate in questa sessione.

## Verifiche locali riprodotte

Le verifiche sono state read-only. Gli hash completi sono riportati qui per consentire una review senza abbreviazioni.

| Oggetto | SHA-256 osservato | Risultato |
|---|---|---|
| Inline loader `INLINE_LOADER` del test | `feef002511128263daf5fa05fcaa72686a688d58fb717b2f8d1a9d026b205021` | PASS: costante estratta senza eseguire test; 626 byte |
| Bootstrap `qualification/alpha-m1-verified-execution-bootstrap.py` | `3a5aa58d0a87b1363e61180773b4fd0318b930d41b81186a39ff0fd40a71f9a6` | PASS contro il pin Sessione 42 |
| Runner `qualification/alpha-m1-verified-evidence-runner.py` | `f67a5dde11add6ced5802fa8091680f75f480a43fcc386f2049844f313b2f469` | PASS contro il pin Sessione 42 |
| Collector `qualification/alpha-m1-vm-evidence-collector.sh` | `6527d34cbfcca75244a77a1457f13e3d8e1b79aee5393f348c73bc96d74a74d2` | PASS contro il pin fisso |
| Verificatore storico `qualification/alpha-m1-evidence-tree-digest.py` | `b003d2b02dcbb02d304371524ef395838599fd918e1505bd100fc2a1340435e3` | PASS: file corrispondente al report Sessione 42 |
| Corpus storico | 149 file; digest `f229f1b367f31053a283f564b3d2fa3edd713bfa1ede5134c899466f1bb8f67f` | PASS: verificatore read-only eseguito direttamente |

Sei artefatti Sessione 34B, ricalcolati localmente e confrontati con i pin completi nel dossier Sessione 41/42:

| Artefatto | SHA-256 |
|---|---|
| Installer | `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7` |
| Manifest | `00fd4036bf0f3873f5c2a0e932c912b4709460fa52c35a1c2564016a92811262` |
| Runner amministrativo | `c68bca67a7a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` |
| Gateway support | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` |
| Canary | `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` |
| Bozza sudoers | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` |

Tutti e sei sono file regolari, link count 1 e uid/gid 1000:1000 nel namespace osservato. Runner, bootstrap, collector e verificatore storico sono anch'essi file regolari a link singolo. Questi controlli dimostrano l'identità dei byte in questo checkout, non autenticità/provenienza, approvazione umana o identità della copia presente sulla VM. Il corpus storico risulta integro rispetto al digest fissato; non prova la provenienza dei binari.

## Riconciliazione delle procedure e limiti di sicurezza

Sono stati letti integralmente i sei documenti richiesti, più le checklist guest/host Sessione 41 e la documentazione runner/bootstrap. Sessione 42 supersede la sintassi di lancio Sessione 39–41. Il comando diretto del runner in Sessione 40 e il pin runner `468756ff…` nel modello Sessione 41 sono storici e non vanno riutilizzati per il runner corrente.

Il comando completo in `alpha-m1-verified-execution.md` esegue **solo verifica del collector**, senza argomenti runner e senza `--execute`. Non è un comando di acquisizione. Sessione 42 precisa inoltre che il nuovo pin runner non eredita le approvazioni precedenti. La procedura Sessione 40 definisce correttamente scope, output e gestione degli esiti, ma il suo esempio di lancio non è più compatibile con la catena verificata Sessione 42.

Revisione statica della catena corrente:

- Inline loader: limita la dimensione dello stdin, verifica l'hash del bootstrap e compila gli stessi byte catturati.
- Bootstrap: richiede path assoluto canonico, apre i componenti con descriptor relativi e `O_NOFOLLOW`, rifiuta symlink, hardlink e file non regolari, controlla stabilità/dimensione/hash e compila gli stessi byte immutabili.
- Runner: richiede il marker del bootstrap, usa solo il collector fissato, apre senza seguire il symlink finale, copia in memfd e verifica i seal `WRITE`, `GROW`, `SHRINK`, `SEAL` e il digest prima di eseguire `/bin/bash` tramite `/proc/self/fd/N`.
- Ambiente: comando esterno previsto usa `env -i`, Python `-I -S`; il figlio ha `PATH`, `LANG`, `LC_ALL`, `TZ` allowlistati. Python, standard library, loader dinamico, shell, `cat`, `env`, kernel, Bash e utility del collector restano nel trust boundary.
- Dipendenze: Python 3/Linux con API `openat`/`O_NOFOLLOW`, `memfd_create`, sealing e `/proc/self/fd`; `/bin/bash` e le utility standard fissate dal PATH del collector. Il container non dimostra disponibilità o comportamento equivalenti sulla VM.
- Il runner inoltra INT/TERM/HUP al process group e attende il figlio; non ha timeout interno. SIGKILL, perdita di alimentazione o errore interpreter/capture possono lasciare evidenza parziale o mancare il record. Conservare ogni output parziale e non ritentare automaticamente.
- Il collector rifiuta root, richiede un output parent esistente, canonico, non symlink, dell'utente corrente e non group/world-writable; crea una singola nuova directory privata mode 0700. Il candidate dir deve essere assoluto, esistente e canonico. Il controllo dei path è pathname-based: non va descritto come eliminazione di ogni race con un attore concorrente dello stesso UID. Directory private sotto controllo dell'operatore e preflight sequenziale sono prerequisiti.
- Il collector effettua scritture solo nella nuova directory di acquisizione; il preflight proposto sotto non scrive alcun file. Il read set è fisso e ampio, come elencato in Sessioni 37/38/40: processi/argomenti e cgroup, inventari/proprietà/journal systemd, metadati di path protetti, inventario D6 solo names-only, hash receipt ove leggibile, cache/build/generated files/artifact hashes e input dell'helper inventory CTest. Deve essere revisionato per intero prima di un'autorizzazione.

Non è stato dimostrato un difetto d'integrità che giustifichi la modifica del runner o del collector; entrambi sono rimasti invariati.

## Matrice dei gate

| Gate | Stato | Evidenza/motivo |
|---|---|---|
| Integrità locale bootstrap, runner, collector e sei artefatti 34B | **PASS locale** | Hash completi ricalcolati; nessuna discrepanza |
| Integrità corpus storico | **PASS locale** | 149 file e digest atteso riprodotti |
| Meccanismo di esecuzione byte-verificata | **PASS tecnico condizionale** | Review statica e Sessione 42; trust boundary esterno non autenticato qui |
| Identità host/VirtualBox e snapshot corrente | **BLOCKED** | Solo dati riferiti; niente host/VBoxManage accessibile |
| Binding VM ↔ guest, machine-id/boot-id e stessa sessione | **BLOCKED** | Guest non osservato; binding non dimostrato |
| Compatibilità runtime e accessibilità del read set sulla VM | **BLOCKED** | Nessun preflight guest; container non sostitutivo |
| Percorso checkout/pin sulla VM | **BLOCKED** | Nessuna copia VM verificata |
| Parent output e capture directory preesistenti, ACL/mount/nomi liberi | **BLOCKED** | Percorsi e permessi guest non osservati né approvati |
| Read set completo revisionato dall'operatore | **BLOCKED** | Nessuna approvazione puntuale ricevuta |
| Comando acquisizione `--execute` revisionato con output esatti | **BLOCKED** | Sessione 42 documenta solo verification-only; path/checkout non disponibili |
| Autorizzazione umana per una singola acquisizione | **BLOCKED** | Modello Sessione 41 vuoto e con pin runner obsoleto |
| Runner/collector reali, test, CTest e campagna privilegiata | **NOT RUN** | Divieti della sessione rispettati |
| Stato storico Alpha M1 | **34 BLOCKED / 7 NOT RUN** | Conservare le classificazioni esistenti; nessuna riclassificazione |
| Installazione/canary/rollback/cleanup/mutazioni amministrative | **NOT RUN / vietati** | Nessuna autorizzazione concessa |

## Procedure preflight read-only per l'operatore

### Host VirtualBox — da eseguire solo sull'host autorizzato

Questa procedura non assume che `VBoxManage` sia installato nel container. Usare output terminale/transcript controllato con timestamp e identità dell'operatore; non scriverlo nel checkout o nella VM. Se l'host non è GNU/Linux, sostituire `hostnamectl` e il formato data con equivalenti read-only nativi. Non eseguire `guestcontrol ... run`.

```sh
date -u --iso-8601=seconds
hostnamectl status
VBoxManage --version
VBoxManage list vms
# Dopo aver identificato e confrontato nome/UUID con l'approvazione:
VM_UUID='e475ea91-72e9-4157-9ee6-583e9ae0462f'
VBoxManage showvminfo "$VM_UUID" --machinereadable
VBoxManage snapshot "$VM_UUID" list --details
# Per ciascun UUID disco rilevato nei controller:
VBoxManage showmediuminfo DISK_UUID
```

Riconciliare UUID/nome, stato stabile, `CurrentSnapshotUUID/Name`, albero snapshot, configurazione e dischi. La checklist completa è [Sessione 41 host preflight](alpha-m1-session-41-host-preflight.md). Non avviare/arrestare VM, cambiare snapshot o modificare medium. Il solo stato `running` non dimostra il guest.

### Guest Debian — da console/sessione guest già autorizzata

Seguire integralmente [Sessione 41 guest preflight](alpha-m1-session-41-guest-preflight.md), producendo solo transcript terminale. In particolare registrare ora UTC, os-release, kernel, identità utente, machine-id, boot-id, PID 1, systemd, virtualizzazione firmware, mount e filesystem. Errori di accesso o bus non vanno aggirati con privilegi.

Per ciascun path candidato, procedere separatamente e fermarsi per review a ogni fase: `namei -l PATH` → review → `stat -c '%F %a %u:%g %n' PATH` → review → `sha256sum PATH` solo se autorizzato. Applicare ai pin e alle destinazioni output. Controllare ACL e mount con `getfacl -p PATH` e `findmnt -T PATH -o TARGET,SOURCE,FSTYPE,OPTIONS`; se non verificabili senza privilegi, gate BLOCKED. Non creare directory, non leggere ricorsivamente D6 e non attraversare fixture/D6. Per il corpus, il verificatore fissato è `python3 -I /reviewed/checkout/qualification/alpha-m1-evidence-tree-digest.py`; invocarlo solo nel checkout VM approvato e conservare il risultato terminale.

Il path candidato riportato dall'operatore `/home/alessandro/monad-alpha-m1-session43-evidence` è dichiarato assente e senza symlink nei componenti esistenti, ma questo non è confermato qui. Inoltre Sessione 40 richiede un **parent già esistente** per l'output del collector e una **capture directory separata già esistente** mode 0700; non creare né usare il path candidato finché non siano stati ispezionati, nominati esattamente nell'approvazione e verificati sul guest. Preflight: `namei`, `stat`, ACL, mount e verifica nomi output assenti. In assenza di approvazione per conservare un report, emettere solo output sul terminale.

## Piano di acquisizione diagnostica successivo

1. Raccogliere le prove host e guest con le procedure sopra e legarle tramite transcript della stessa sessione autorizzata; verificare VM/snapshot e guest ID senza cambiare stato.
2. Nel checkout effettivamente presente sul guest, rifare in sequenza controllata la verifica di path e hash per inline loader, bootstrap, runner, collector, verificatore storico e sei file 34B. Confrontare hash completi con questa baseline e revisionare esplicitamente tutti i byte.
3. Completare la verifica 149/digest storico e ispezionare path, proprietà, ACL, mount, runtime/dipendenze e read set. Per D6 limitarsi all'inventario names-only documentato; preservare residual, fixture, log, receipt, backup e tutti i file storici.
4. Designare un output parent già esistente e una capture directory distinta, entrambe fuori da checkout/evidenze storiche/fixture/D6/path amministrativi. Verificare owner, mode, ACL, mount, componenti senza symlink e assenza dei nomi stdout/stderr/status. Nessuna directory viene creata durante il preflight.
5. Revisionare un comando `--execute` completo basato sul loader Sessione 42, con percorso checkout assoluto del guest e tutte le destinazioni letterali approvate. Sessione 42 non fornisce questo comando: il testo in appendice è solo una derivazione parametrica per review, non lanciarla né approvarla automaticamente.
6. Compilare da zero il modello in appendice con identità e hash correnti e read set completo. Solo un responsabile umano può autorizzare una singola acquisizione diagnostica non-root dopo la chiusura dei gate. Registrare stdout, stderr, exit status supervisor e record runner in file nuovi no-clobber; conservare qualsiasi output incompleto senza retry.
7. Dopo eventuale acquisizione autorizzata, revisionare ogni `checks.tsv`, report e file raw; conservare PASS/FAIL/BLOCKED/TIMEOUT/NOT RUN come osservati. Una successiva qualificazione richiede una decisione separata e l'intero campaign/evidence review approvato.

### Forma del comando richiesto — NON ESEGUIRE

Non esiste ancora un comando operativo esatto e approvato: il path assoluto del checkout guest, output parent, capture directory, nomi riservati e autorizzazione non sono stati dimostrati. Il comando verification-only della Sessione 42 non va trasformato mentalmente in acquisizione. La forma da sottoporre a review, ottenuta mantenendo il loader inline fissato e aggiungendo i soli argomenti previsti dall'API del bootstrap/runner, è:

```sh
set -o pipefail
set -o noclobber
exec 3> /APPROVED/CAPTURE/runner.exit-status
/usr/bin/cat -- /APPROVED/CHECKOUT/qualification/alpha-m1-verified-execution-bootstrap.py |
  /usr/bin/env -i PATH=/usr/bin:/bin LANG=C LC_ALL=C TZ=UTC \
  /usr/bin/python3 -I -S -c 'import hashlib,sys
expected=sys.argv[1]
source=sys.stdin.buffer.read(262145)
if len(source)>262144 or sys.stdin.buffer.read(1):
 print("ERROR: bootstrap source exceeds size limit",file=sys.stderr);raise SystemExit(70)
actual=hashlib.sha256(source).hexdigest()
if actual != expected:
 print("ERROR: bootstrap SHA-256 mismatch",file=sys.stderr);raise SystemExit(70)
args=sys.argv[1:]
sys.argv=["<verified-bootstrap>",*args]
ns={"__name__":"__main__","__file__":"<verified-bootstrap>","__package__":None,"__alpha_m1_verified_bootstrap_sha256__":actual}
exec(compile(source,"<verified-bootstrap>","exec",dont_inherit=True),ns,ns)
' \
  3a5aa58d0a87b1363e61180773b4fd0318b930d41b81186a39ff0fd40a71f9a6 \
  f67a5dde11add6ced5802fa8091680f75f480a43fcc386f2049844f313b2f469 \
  /APPROVED/CHECKOUT/qualification/alpha-m1-verified-evidence-runner.py \
  --execute --output-parent /APPROVED/EXISTING/EVIDENCE-PARENT \
  --candidate-dir /APPROVED/CHECKOUT/qualification \
  > /APPROVED/CAPTURE/runner.stdout 2> /APPROVED/CAPTURE/runner.stderr
rc=$?
printf '%s\n' "$rc" >&3
exec 3>&-
```

I segnaposto non sono valori approvati e il blocco non è eseguibile così com'è. Nella review finale verificare di nuovo che i 626 byte di codice inline siano identici alla costante testata, il cui digest è `feef002511128263daf5fa05fcaa72686a688d58fb717b2f8d1a9d026b205021`; usare i pin completi sopra e il path guest reale. `noclobber` non rende sicuri directory o componenti symlink: l'ispezione sequenziale e la prenotazione approvata dei nomi restano obbligatorie. Redirection failure, errore di lancio, timeout o output ambiguo comportano stop e conservazione dello stato; nessun retry automatico. Questa derivazione non colma la mancanza di una review finale del comando completo.

## Modello di autorizzazione compilabile

Il modello seguente è intenzionalmente vuoto. Va copiato nel canale approvato dell'operatore e compilato dopo i preflight; questo report non è un'autorizzazione.

```text
Richiedente/operatore e identificativo: ______________________________
Responsabile approvatore/ruolo: _____________________________________
Timestamp UTC e finestra: ___________________________________________
Host VirtualBox e prova/transcript: __________________________________
Nome VM / UUID / stato osservato: ____________________________________
Snapshot corrente o altro restore point / UUID: ______________________
Guest OS / machine-id / boot-id / kernel / prova di binding: __________
Checkout guest assoluto / branch / HEAD: _____________________________
Inline loader SHA-256: feef002511128263daf5fa05fcaa72686a688d58fb717b2f8d1a9d026b205021
Bootstrap SHA-256: 3a5aa58d0a87b1363e61180773b4fd0318b930d41b81186a39ff0fd40a71f9a6
Runner SHA-256: f67a5dde11add6ced5802fa8091680f75f480a43fcc386f2049844f313b2f469
Collector SHA-256: 6527d34cbfcca75244a77a1457f13e3d8e1b79aee5393f348c73bc96d74a74d2
Verificatore storico: 149 file / f229f1b367f31053a283f564b3d2fa3edd713bfa1ede5134c899466f1bb8f67f
Read set completo Sessioni 37/38/40 revisionato (riferimenti): __________
Output parent preesistente / owner / mode / ACL / mount: ______________
Capture dir separata preesistente / owner / mode 0700 / ACL / mount: ___
Nomi stdout, stderr, status e assenza verificata: ______________________
Utente guest non-root: __________________ Una sola acquisizione: [ ]
Comando completo finale revisionato e allegato: [ ]
Trasferimento, custodia e retention output: ___________________________

AUTORIZZO una sola acquisizione diagnostica non-root: [ ] SÌ [ ] NO
Operazioni consentite: solo runner verificato + collector fissato, read set
sopra e nuovi output nei path elencati. Nessun altro scope.
Firma/registrazione e data UTC: ______________________________________

STOP: mismatch identità/hash; symlink/path/mode/ACL/mount inattesi; read
scope non revisionato; privilegi richiesti; errori, segnale, timeout o output
parziale/ambiguo; cambiamento systemd/cgroup/mount/D6/fixture/storico.
In caso di stop: preservare output/stato, non correggere, non pulire, non
riprovare e non proseguire. Nessuna autorizzazione a CTest, test privilegiati,
canary, staging, installazione, rollback, cleanup o mutazioni amministrative.
```

## Revisioni indipendenti

- **Core — PASS locale:** hash/pin correnti e architettura byte-verificata coerenti; nessun difetto che richieda modifiche. Nota che non esiste un comando `--execute` esatto revisionato nei documenti correnti.
- **Security — PASS condizionale sul byte invariant; BLOCKED operativo:** trust boundary, memfd, output, symlink, environment e stop conditions coerenti; nessuna autorizzazione. Ha evidenziato i limiti pathname/race e la necessità di approvazione esterna dei pin.
- **Verification — PASS locale / BLOCKED acquisizione:** pin e corpus riprodotti; separazione fra evidenza diretta e dati riferiti; runner/collector/test non eseguiti. Storia 34 BLOCKED e 7 NOT RUN invariata.
- **Operations — BLOCKED:** checklist host/guest utilizzabili, ma ambiente corrente senza VBoxManage e non target; output path e approvazione mancanti; comando acquisizione Sessione 42 non documentato in forma finale.

Le revisioni sono indipendenti e concordano sulla decisione BLOCKED. Nessuna costituisce approvazione umana.

## Decisione finale e autorizzazioni

**BLOCKED.** È consentito all'operatore autorizzato completare i preflight read-only host e guest e sottoporre hash, path e read set a review. Una sola acquisizione potrà essere considerata solo dopo binding VM/guest verificato, checkout e pin correnti riconfermati, output/capture directories esistenti e approvate, comando completo revisionato e approvazione umana esplicita.

Restano vietati senza approvazioni separate: esecuzione runner/collector ora; runner con `--execute`; test o CTest, in particolare privilegiati; canary; staging/installazione; rollback; cleanup; modifiche systemd/cgroup/mount/permessi/ownership/ACL; operazioni VM/snapshot; qualsiasi scrittura nelle evidenze storiche, fixture o D6; commit e push. Nessun claim di qualifica Alpha M1 è autorizzato.
