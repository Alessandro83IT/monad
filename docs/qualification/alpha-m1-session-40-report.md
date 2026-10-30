# Monad Alpha M1 — Sessione 40: integrity, runner audit e readiness acquisizione

**Decisione: NO-GO per l'acquisizione da questo ambiente.** I componenti locali per una raccolta diagnostica hanno superato i gate software, ma l'ambiente VM, la destinazione output e l'autorizzazione operativa non sono disponibili. La procedura è pronta per una successiva decisione esplicita dell'operatore. Alpha M1 resta **BLOCKED**, installazione **NON AUTORIZZATA**. Nessun collector reale è stato eseguito.

Durata effettiva stimata: circa 27 minuti. Non si è atteso il limite pianificato.

## Baseline e preflight

- Ambiente: Debian GNU/Linux 13.7, kernel `6.12.113+deb13-amd64`, x86_64, `systemd-detect-virt=container-other`, uid/gid 1000 (`alessandro`). `VBoxManage` non è disponibile. Host VirtualBox, VM UUID, stato VM e snapshot non osservati.
- Repository `/home/alessandro/Projects/monad`, branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939` come previsto. Il working tree presentava nove modifiche tracked e numerosi file/build untracked già presenti. Non sono stati ripristinati o ripuliti; nessuna modifica tracked è stata fatta in Sessione 40.
- Letti i rapporti 34B–39 rilevanti, il record di preflight Sessione 38 e le procedure Sessioni 37–39. I 149 artefatti storici non sono stati modificati.
- Al preflight i digest richiesti coincidevano: collector `6527d34cbfcca75244a77a1457f13e3d8e1b79aee5393f348c73bc96d74a74d2`; runner Sessione 39 `0151e13f7f6bf391ba6609e33cf7e2ba6d442faf5938a926edc333ac36d05891`.
- Durante l'audit è stata motivata una correzione al runner (verifica autonoma dei seal e rehash del descriptor immediatamente prima dell'esecuzione). L'hash finale è quindi cambiato: runner finale `468756ff8cc1f55e09e4560ec1e47974b53a076219715fa35bf36874fb2cc618`. Questo digest è stato revisionato indipendentemente in Sessione 40, ma deve essere inserito nell'approvazione futura; il pin Sessione 39 non va riutilizzato.
- Il preflight finale del runner in modalità predefinita ha stampato il digest collector atteso e `verification_only=true`. Non è stato passato `--execute`.

## Ricostruzione del digest storico

Il metodo non era assente dalla documentazione: il rapporto Sessione 37 e il record Sessione 38 dichiarano che fu usato lo stesso procedimento descritto in `docs/qualification/alpha-m1-session-38-manual-acquisition.md`, sezione 6. Il codice deterministico alimenta l'hash aggregato, in traversata depth-first e ordinamento di ogni directory tramite `os.fsencode`, con ogni file nel formato:

`uint64 big-endian lunghezza percorso relativo || byte percorso || uint64 big-endian dimensione || contenuto byte-per-byte`.

I path usano `/`; i digest per-file sono stampati separatamente e non sono l'input aggregato. Directory, permessi, owner, timestamp e altri metadata non entrano nell'hash. La procedura contemporanea Sessione 38 afferma che lo stesso algoritmo aveva prodotto la baseline; il test attuale ne riproduce esattamente count e digest. Non è una firma esterna né una prova crittografica dell'esecuzione storica al tempo della Sessione 35.

Creato `qualification/alpha-m1-evidence-tree-digest.py`, limitato al percorso storico canonico. Apre componenti e file senza seguire symlink, rifiuta symlink e file speciali, verifica identità/metadata dei file, ricontrolla nomi e metadata delle directory e fallisce su count/hash diversi. La verifica read-only ha riportato **149 file** e SHA-256 aggregato `f229f1b367f31053a283f564b3d2fa3edd713bfa1ede5134c899466f1bb8f67f` (**PASS**).

L'hash `4eb691…` tentato in Sessione 39 aggregava righe di output di `sha256sum` con framing diverso. Non indica una discrepanza dei payload ed è stato corretto come interpretazione in questo rapporto; il rapporto storico Sessione 39 è preservato come resoconto contemporaneo della sua verifica non comparabile.

Limiti del framing storico: directory vuote e metadata non sono coperti; non è attestazione esterna e un attore privilegiato può modificare corpus e riferimenti locali. La traversata rifiuta cambiamenti concorrenti osservabili ma non è uno snapshot filesystem atomico.

## Audit del runner e verifica avversariale

Il runner usa `os.memfd_create` con `MFD_CLOEXEC | MFD_ALLOW_SEALING`. Copia dal descriptor sorgente aperto con `O_NOFOLLOW`, controlla file regolare/hardlink singolo e metadata prima/dopo, hasha la copia contro il pin. Applica e rilegge tutti e quattro i seal richiesti: `F_SEAL_WRITE`, `F_SEAL_GROW`, `F_SEAL_SHRINK`, `F_SEAL_SEAL`. Prima di lanciare Bash, `execute_snapshot` ricontrolla i seal e ricalcola il digest del descriptor con `pread` contro il pin incorporato; non accetta più un digest di esecuzione fornito dal chiamante. L'invocazione CLI seleziona un solo collector repository-relative e non espone un percorso script.

Il descriptor sigillato è passato esplicitamente al fixed `/bin/bash` con `close_fds=True`, `pass_fds=(snapshot_fd,)` e sessione/process group dedicato. Il descriptor deve restare visibile a Bash per leggere lo script e può essere ereditato dai comandi figli: è read-only e contiene i byte pinned, ma il runner non nasconde quel descriptor né pinna i binari degli strumenti esterni. L'ambiente figlio è allowlistato (`PATH`, locale e timezone), senza `BASH_ENV`, funzioni esportate o variabili loader ereditate. SIGINT/SIGTERM/SIGHUP vengono inoltrati al process group; il wait status e il segnale gestito sono inclusi nel record JSON su stderr.

Il record non è un'attestazione durevole o tamper-resistant. Un guasto del canale può lasciare un record parziale/assente; SIGKILL, crash o perdita di alimentazione non sono registrabili dal processo. Processi che si staccano dal process group non sono garantiti terminati. Il kernel, `/proc`, interprete Bash, strumenti esterni, input letti dal collector e output capture restano fuori dall'hash del collector. L'output privato e no-clobber è responsabilità della procedura/operator approval.

Test sintetici eseguiti: `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v tests.alpha_m1_evidence_tree_digest_test tests.alpha_m1_verified_evidence_runner_test` — **20 PASS, 0 FAIL, 0 SKIP**. Copertura digest: framing/order deterministici, path/root inatteso, count/hash su file mancanti/aggiuntivi, symlink/special file, mutazioni file e directory durante la traversata. Copertura runner: hash/seal corretti/errati, descriptor non sigillato, path substitution, mutazione concorrente, symlink/hardlink, errori open/read, ambiente contaminato, segnali e processi figli, output, exit nonzero, CLI verification-only e argomenti script non ammessi. Nessuna fixture è il collector reale.

## Provenienza sorgenti-binari

Esaminate in sola lettura le directory locali `build-monad-debug` e `build-monad-release`; non sono state ricompilate e i binari non sono stati eseguiti.

| Build locale | Cache/configurazione osservata | Hash artefatto `mpkg` | Hash CMakeCache / `build.ninja` | Valutazione |
|---|---|---|---|---|
| Debug | source `/home/alessandro/Projects/monad`, Ninja, Debug, `/usr/bin/c++`, GNU 14.2.0 | 17,224 byte; `60b58e37eb687d4d25df09e0e3e37e1524f4fd289c7a3600b6ab7c76dceae7f0` | `4c4b8c718398561b5b2b467ec4b436b95360fddebad36b968b155b07c75fdcb5` / `46304ed4c7a074ef3c251eca7e164700c31e443e94479d30fd4ff58b133ccb44` | Parziale |
| Release | source `/home/alessandro/Projects/monad`, Ninja, Release, `/usr/bin/c++`, GNU 14.2.0 | 15,960 byte; `4c51b175cb08072c990d2c1d4c72f767a98a24742c088891b167bfcdbadf0bf1` | `fd02c07414da9a172fb51bc20731da3e3716c3ee471684e23bcdb188aef299b7` / `e0b1c8c1788b9ae2aa0d2f4b4ed3bc414a093abe74ec37c98885a917c0aeec07` | Parziale |

Cache, generated Ninja rules, `.ninja_log`, object/output mtimes e artifact hashes sono coerenti con configurazioni locali e mostrano build activity. Ma HEAD `abd6bac…` non equivale al contenuto sorgente compilato: il checkout è dirty; non esiste source-tree digest/patch legato al build, attestazione indipendente, comando/ambiente originale autenticato o catena input→processo→output. I file `build.ninja`/verify-globs osservati sono successivi ai binari `mpkg`, quindi non attestano quali regole fossero presenti al linking. Gli hash identificano i binari correnti, non la loro origine.

I percorsi previsti dalla campagna VM (`/opt/athena-qualification/build` e `/tmp/monad-s32-verification-final4-20261021`) non sono questi build locali. Il primo è stato storicamente descritto come cache con source `/opt/athena-qualification` senza Git metadata; la Sessione 38 dice che tali percorsi nel container non sono stati letti. Provenienza VM: **non verificabile**. Provenienza locale: **parziale**. Questa lacuna non impedisce una raccolta diagnostica read-only approvata, ma blocca l'uso degli artefatti per qualificazione finale.

Resta inoltre irrisolta la discrepanza storica dei totali CTest (140 vs 147); i 18 nomi privilegiati derivano dal manifest e non dimostrano l'inventario generato. Sessione 40 non ha eseguito CTest né letto i `CTestTestfile.cmake` dei build VM.

## Gate GO / NO-GO

| Gate | Esito | Evidenza e significato |
|---|---|---|
| G1 — Collector e digest | **GO locale** | Collector presente; SHA-256 coincide con il pin fisso. Solo identità byte locale. |
| G2 — Runner e digest | **GO software / approvazione nuova richiesta** | Hash finale `468756ff…cc618`, review Security/Core e test PASS. È diverso dal pin Sessione 39 perché corretto in questa sessione; l'operatore deve approvare questo esatto digest. |
| G3 — Storico | **GO locale** | Algoritmo documentato ricostruito; 149 file e digest storico riprodotti. Non è attestazione esterna e metadata non sono coperti. |
| G4 — Sicurezza runner | **GO con limiti documentati** | Sealing, rehash del descriptor prima del launch, ambiente minimo e signal handling revisionati; test avversariali PASS. Bash, tools esterni, kernel e canali output non sono pinned dal runner. |
| G5 — Output isolati/tracciabili | **GO procedurale / non predisposti** | Procedura usa evidence parent preesistente, capture separata, file no-clobber e FD status riservato. Percorsi reali non sono stati approvati o verificati. |
| G6 — VM identificata | **NO-GO** | Solo container visibile; nessun VBoxManage, UUID o snapshot. |
| G7 — Accessi compatibili | **NO-GO runtime** | EUID ordinario osservato nel container, ma non esiste sessione guest autenticata né verifica accessi sulla VM. Il read set completo va approvato; i read protetti richiedono scope separati. |
| G8 — Procedura read-only | **GO documentale** | Procedura Sessione 40 revisionata Operations; nessun test, CTest o mutazione inclusi. |
| G9 — Provenienza build | **PARTIAL** | Sufficiente a supportare una raccolta diagnostica che registri e classifichi le lacune; insufficiente a promuovere i binari o qualificare Alpha M1. Build VM non osservati. |
| G10 — Autorizzazione operatore | **NO-GO** | Nessuna autorizzazione esplicita per eseguire il collector o scope di lettura acquisito. |

**GO tecnico dei componenti software per preparare una raccolta diagnostica:** sì, limitato ai digest, verifier e runner correnti revisionati. **GO tecnico dell'acquisizione su VM:** no, perché G6/G7 non dimostrati. **GO operativo:** NO-GO per G6, G7 e G10; manca anche la predisposizione dei path G5. La provenienza parziale (G9) non vieta di raccogliere diagnostica approvata, ma resta gate di qualificazione.

## Revisioni indipendenti

- **Core — PASS:** audit finale del runner hash `468756ff…cc618`; il descriptor eseguito viene rehashato al pin e i seal sono verificati. Provenienza locale dei build parziale e build VM non verificabile. Nessun binario eseguito/rebuild.
- **Security — PASS con limiti:** nessun blocker sui byte eseguiti; confermati sigilli, fixed digest e ambiente. Residui: descriptor ereditabile dai child tools, record output non durevole/tamper-proof, processi detached/SIGKILL, Bash/kernel/tools fuori dal pin. Nessuna autorizzazione all'esecuzione.
- **Verification — PASS:** digest tree riprodotto; suite combinata **20/20 PASS**, nessun file storico alterato. Nessuna evidenza di firma esterna o controllo dei metadata storici.
- **Operations — NO-GO operativo:** procedura e output capture shape revisionate; container, identity/snapshot, percorsi output e autorizzazione mancano. Nessun collector eseguito.

## File creati o modificati in Sessione 40

- Creati: `qualification/alpha-m1-evidence-tree-digest.py`, `qualification/alpha-m1-evidence-tree-digest.md`, `tests/alpha_m1_evidence_tree_digest_test.py`, `docs/qualification/alpha-m1-session-40-acquisition-procedure.md`, questo rapporto.
- Modificati: `qualification/alpha-m1-verified-evidence-runner.py`, `qualification/alpha-m1-verified-evidence-runner.md`, `tests/alpha_m1_verified_evidence_runner_test.py`.
- Il collector e i file storici restano invariati. Nessun build directory è stato modificato.

## Decisione finale e condizioni successive

**NO-GO per acquisizione in Sessione 40.** Per riesaminare la decisione servono: host autorizzato con VM UUID/snapshot verificabili e legati a machine-id/boot-id guest; approvazione registrata dell'esatto runner SHA-256 `468756ff8cc1f55e09e4560ec1e47974b53a076219715fa35bf36874fb2cc618` e collector SHA-256 fisso; approvazione dell'intero read set non selettivo e delle letture privilegiate separate; evidence parent/capture paths privati già esistenti e verificati; decisione umana esplicita sull'esecuzione non-root; no-clobber capture e conservazione del wait status.

La procedura dettagliata è [Session 40 acquisition procedure](alpha-m1-session-40-acquisition-procedure.md). Il digest tree attuale deve essere verificato prima dell'esecuzione e i 149 file vanno mantenuti byte-per-byte invariati. I 34 controlli storici BLOCKED e i 7 NOT RUN restano tali. Nessun collector, CTest Alpha, canary, installazione, rollback, cleanup, comando privilegiato, modifica systemd/cgroup/mount, ripristino snapshot, commit o push è stato eseguito.
