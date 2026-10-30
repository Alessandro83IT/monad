# Alpha M1 — consolidamento amministrativo e canary

> Nota storica: risultati CTest e digest in questa nota appartengono alla revisione precedente del 2026-10-21. Per hash correnti dopo staging TOCTOU e gestione systemd, consultare [alpha-m1-gateway-hardening-report.md](alpha-m1-gateway-hardening-report.md). Non usare questi digest per promozione. Nessun test privilegiato viene attestato da questa nota.

## Esito della revisione locale

Preparazione amministrativa e test statici completati nel workspace. Alpha M1 **non è qualificata**: nessun comando root, canary systemd, installazione sudoers/runner o test privilegiato è stato eseguito qui.

Il child passato a `python3 -c` è un interprete distinto e riceve il percorso ext4 come ultimo argomento (`sys.argv[1:]`); il probe usa `os.path.join(ext4_parent, ...)`. La fixture è stata allineata in canary, runner e installer a `/var/lib/athena-alpha-m1/ext4-fixture`. Installer e runner controllano che sia una directory root-owned `0700`, ACL-free e vuota, su ext4; l'installer non formatta e non monta dispositivi.

Il canary conserva `PrivateTmp=yes` e controlla entrambe le direzioni dell'isolamento: il child non deve vedere il marker dell'host, e il probe creato nel `/tmp` privato non deve comparire nel `/tmp` host. Il child verifica inoltre che `/var/lib/athena-alpha-m1`, la directory backups, il log e il build siano montati read-only e rifiutino una creazione di file; l'unico percorso persistente dichiarato scrivibile è la fixture ext4. La preparazione delle prove è dentro il blocco di gestione errori; timeout, richiesta di stop, reset dell'unità e rimozione della directory temporanea vengono gestiti separatamente, con errori di cleanup segnalati.

L'orchestratore `qualification/run-alpha-m1-privileged.py` confronta prima l'inventario CTest con l'elenco statico dei 18 test nell'ordine CTest osservato. Per ciascun caso invoca in sequenza solo `sudo -n /usr/local/sbin/athena-alpha-m1-runner CASE`, salva exit code, durata e output, e prosegue per raccogliere tutti gli esiti. Il runner continua a imporre la propria allowlist e gli hash degli artifact autenticati.

## Controllo sudoers

Revisione testuale: il Cmnd_Alias contiene 18 righe di comando esatto, tutte sul medesimo runner, con un solo argomento fisso per riga. Non sono presenti wildcard, shell, opzioni libere o il canary. L'utente `alessandro` riceve solo quel Cmnd_Alias come root NOPASSWD. Il test statico verifica uguaglianza e unicità tra le righe sudoers, le chiavi di `ALLOWLIST` e l'elenco dell'orchestratore. `visudo` non è disponibile in questo workspace: la validazione sintattica effettiva resta richiesta sulla VM prima/dopo l'installazione.

## Hash amministrativi

| File | SHA-256 |
|---|---|
| `qualification/athena-alpha-m1-runner.py` | `5d6ee35070954d97034de7f0323204995fe0ca86ed12fad774a3acb9ada021d5` |
| `qualification/athena-alpha-m1-systemd-canary.py` | `5208ad30d0a55e705ac78fee205cdf6731a4f35fa13089dd0728af8cc8696cfd` |
| `qualification/athena-alpha-m1-sudoers` | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` |

I primi due hash sono anche pin dell'installer; il pin sudoers rimane invariato. L'hash fornito per il candidato in `/tmp` era `b35e87bd4f9d82e87291bd334d22040e54a8404a111c3c406d932e47c410dd0c`, ma il file risultava owner `nobody`, mode `0600` e non leggibile dall'utente del workspace. Non è stato copiato, alterato o dichiarato verificato. Il sorgente repository è stato consolidato e il suo hash è quello riportato sopra.

## Installazione e rollback

I dettagli operativi e il comando unico per i 18 test sono in [alpha-m1-admin-staging.md](alpha-m1-admin-staging.md). L'installer verifica hash, ACL, receipt del canary, artifact, fixture, backup e sintassi sudoers prima dello swap; conserva lo stato precedente e prova rollback indipendente per runner e sudoers dopo errori post-marker. Su rollback parziale conserva journal e backup e richiede recupero amministrativo manuale. Non sono state eseguite prove dinamiche di queste procedure: la revisione qui è statica.

## Verifiche locali e limiti

- `tests/alpha_m1_canary_child_test.py`: compilazione del child, controllo simboli globali, passaggio fixture, proprietà `PrivateTmp`, percorsi di sola lettura e gestione timeout/cleanup.
- `tests/alpha_m1_admin_static_test.py`: uguaglianza delle 18 allowlist e coerenza dei percorsi fixture.
- `tests/alpha_m1_orchestrator_test.py`: esecuzione sequenziale simulata, raccolta di exit code/log e rifiuto dell'inventario inatteso.
- `py_compile` per runner, canary, orchestratore e test; `bash -n` per installer.
- CTest non privilegiato in `build-monad-debug`: 122 casi selezionati, 11 skip configurati, un errore packaging riproducibile (`packaging_regression_test`). Il primo run includeva anche un errore della source-closure suite perché i nuovi test non erano ancora nell'inventario; dopo l'aggiornamento ordinato di `tests/bounded_toml_sources.txt`, `qualification_driver_test.py` passa (7/7). Nessuno dei 18 privilegiati è stato eseguito.
- Nessun test privilegiato è stato eseguito. Per completare la qualificazione servono il canary con receipt nella VM, `visudo -c`, installazione manuale revisionata, orchestrazione dei 18 test e conservazione dei risultati originali. Qualsiasi skip, errore sudo o assenza di evidenza non conta come PASS.

## Revisione del ripristino canary

Il confronto diretto richiesto con `/root/athena-alpha-m1-stage/athena-alpha-m1-systemd-canary.py` non è disponibile in questo workspace: la lettura restituisce `Permission denied`. Lo staging non è stato modificato. Nel sorgente del workspace sono mantenuti il passaggio di `EXT4_PARENT`, `PrivateTmp=yes` e l'eccezione scrivibile limitata a `/tmp` privato e fixture ext4. I controlli del child includono marker host/privato, mount read-only e tentativi di scrittura sotto build, log, stato Alpha M1 e backups; fixture ext4 rimane provata separatamente come scrivibile.

Esito delle verifiche eseguite dopo le modifiche: `py_compile` sui sorgenti e test Python pertinenti riuscito; `bash -n` sull'installer riuscito; CTest mirato riuscito (4/4: `qualification_driver_test`, `alpha_m1_canary_child_test`, `alpha_m1_admin_static_test`, `alpha_m1_orchestrator_test`). Queste verifiche sono statiche/non privilegiate e non attestano l'effettivo comportamento del namespace systemd sulla VM.
