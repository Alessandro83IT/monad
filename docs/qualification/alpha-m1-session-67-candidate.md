# Alpha M1 — Session 67 candidate

**Esito supervisore: PASS sintetico con condizioni. Esecuzione Codex reale: NOT RUN. Alpha M1: BLOCKED.** Il supervisore può ora essere lanciato direttamente da una shell Debian. In Sessione 67 non è stato avviato Codex annidato, né è stata avviata una catena di sessioni.

## Architettura e codice implementato

`tools/codex-supervisor/supervisor.py` supporta l'uso host-orchestrated senza una sessione Codex principale. Il percorso del CLI è selezionabile con `--codex-bin` o `MONAD_CODEX_BIN`; i percorsi assoluti funzionano con un `PATH` ridotto. `preflight` risolve l'eseguibile, richiede l'identità esatta `codex-cli 0.164.0`, valida schema e digest del piano, verifica `git` e i comandi di test dichiarati, e prova con file esclusivi le directory di stato e temporanee. Il controllo versione usa un gruppo di processi separato, home/cache temporanei isolati e output limitato a 64 KiB.

Ogni esecuzione usa una directory UUID per log e rapporto, un `CODEX_HOME` nuovo, un `HOME` e percorsi `XDG_*` isolati e una directory temporanea unica. I test ricevono lo stesso ambiente isolato e `PYTHONDONTWRITEBYTECODE=1`. Il codice rifiuta un `MONAD_CODEX_HOME` già esistente; un home esplicito creato per un run è conservato al percorso configurato. Il `CODEX_HOME` ereditato dall'host non viene riutilizzato. Non si copiano profili o credenziali preesistenti.

I subprocess Codex e test partono in nuovi gruppi. Timeout e stop terminano il gruppo con TERM e poi KILL entro attese limitate; gruppi residui mantengono il run in `RUNNING`, preservano la directory temporanea e bloccano la recovery automatica. Prima di `Popen` è scritto lo stato `STARTING`, poi si registrano PID e gruppo. `resume` non rilancia run incompleti o ambigui.

Il piano dichiarativo mantiene autorizzazione esplicita e digest SHA-256 dei byte completi. La nuova sezione `limits` specifica timeout massimo, numero massimo di percorsi Git modificati e byte massimi di output. Il timeout richiesto oltre il limite è rifiutato. L'output di run, home Codex e scratch è misurato durante l'esecuzione e al termine; link simbolici, file speciali e directory non enumerabili rendono il run non qualificato. Un override della directory di stato dentro il repository è ammesso solo per `.codex-supervisor`, così non può nascondere cambiamenti al perimetro Git.

Questi limiti di output sono controlli periodici, non quote filesystem: una scrittura rapida può superare la soglia tra due campioni. Il controllo Git non isola il processo e non rileva file ignorati, modifiche transitorie ripristinate, percorsi esterni o processi che evadono il gruppo con `setsid()`.

## File modificati

- `tools/codex-supervisor/supervisor.py`
- `tools/codex-supervisor/test_supervisor.py`
- `tools/codex-supervisor/milestones.json`
- `tools/codex-supervisor/README.md`
- `docs/qualification/alpha-m1-session-67-candidate.md`

Gli altri cambiamenti tracciati, directory build, cache, rapporti e artefatti non tracciati già presenti nel worktree sono stati preservati. Le due cache preesistenti `tools/codex-supervisor/__pycache__/*.pyc` non sono state ripristinate né rimosse.

## Test e verifiche

| Verifica | Esito | Dettaglio |
|---|---|---|
| Suite sintetica supervisore | **PASS — 49 test** | `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/codex-supervisor -p 'test_*.py' -v`; exit 0. |
| Errori/timeout della suite finale | **FAIL 0; TIMEOUT 0** | Un difetto del fixture di test per home isolata è stato corretto prima dell'ultima esecuzione completa. |
| PATH ridotto, CLI assoluto | **PASS** | Preflight e run con il Codex simulato. |
| CODEX_HOME e directory temporanee non scrivibili | **PASS** | Preflight e run si fermano prima dell'esecuzione; file preesistenti conservati. |
| Cache `.pyc` | **PASS** | Digest invariati; i comandi di test impostano `PYTHONDONTWRITEBYTECODE=1`. |
| Timeout, stop, crash e processi figli | **PASS** | Timeout/stop terminano gruppi; i test verificano che i figli sintetici non restino vivi; stato incompleto non viene rilanciato. |
| Piano alterato, milestone non autorizzata, stato corrotto | **PASS** | Rifiuti fail-closed coperti da test. |
| Modifiche fuori perimetro e limiti dichiarati | **PASS** | Rilevati cambiamenti protetti/fuori scope, superamento timeout, numero percorsi e output, traversal non sicuro. |
| `plan` del piano predefinito | **PASS** | Schema aggiornato valido; nessuna autorizzazione o esecuzione avviata. |
| CLI reale 0.164.0 / preflight host reale | **NOT RUN** | La Sessione 66 aveva verificato la versione, ma il tentativo annidato era stato bloccato dal filesystem read-only. In Sessione 67 si sono usati esclusivamente CLI simulati. |
| CMake/CTest Monad | **NOT RUN** | Non richiesto per le modifiche Python. |
| Sudo, test privilegiati, collector, diagnostica, payload D6, installazione, promozione digest, commit/push | **SKIP / NON ESEGUITI** | Fuori dai limiti della sessione. |

Non è stato eseguito alcun comando Codex reale, compreso un probe annidato. La verifica di `preflight` e dei processi versione è sintetica.

## Review Core, Security e Operations

- **Core — PASS con limiti documentati.** Ha richiesto correzioni per output ricorsivo, cleanup con processi residui, stato `STARTING`, validazione prerequisiti e isolamento del profilo. Le correzioni sono state integrate; la review finale non ha lasciato difetti concreti aperti. Ha confermato che il limite byte è monitorato e può essere superato tra campioni.
- **Security — PASS con limiti documentati.** Ha richiesto versione CLI esatta, pulizia dei gruppi di processo del controllo versione, home isolata, conteggio di output annidato e protezione dalle directory di stato interne al repository che avrebbero potuto occultare modifiche. Le correzioni hanno test dedicati. Nessuna review considera il supervisore un confine di contenimento o il testo della versione un'autenticazione del binario.
- **Operations — PASS con limiti documentati.** Ha verificato conservazione di log/rapporti/home, cleanup condizionato all'assenza dei gruppi, recovery senza rilancio e coerenza della documentazione su home esplicita e soglia di output. Nessuna esecuzione operativa è stata raccomandata o effettuata.

## Proprietà dimostrate e limiti residui

I test dimostrano fail-closed per CLI simulato assente/non eseguibile/versione errata, PATH ridotto, directory non scrivibili, cache non riscrivibili, timeout, stop, figli residui, recovery ambigua, stato e piano corrotti o alterati, milestone non autorizzate, modifiche fuori perimetro e limiti superati. La suite non dimostra isolamento contro processi same-UID, autenticità del binario, contenimento di processi che evadono il gruppo, quota disco rigida, comportamento del Codex reale o durabilità in caso di perdita di alimentazione.

Autenticazione Codex basata solo su un profilo OAuth già presente non è supportata dal run isolato: l'operatore deve predisporre un metodo di autenticazione compatibile con l'ambiente, senza copiare profili o credenziali esistenti. L'override `MONAD_CODEX_HOME` è single-use perché il percorso creato resta conservato.

## SHA-256 degli artefatti

Digest locali per integrità del candidato; **non sono pin promossi**. I digest delle cache coincidono con quelli osservati prima delle modifiche Sessione 67.

| Artefatto | SHA-256 |
|---|---|
| `tools/codex-supervisor/supervisor.py` | `33fe939b6004958976f3624c2feecf5590f854910a788716664422d0f47fde4b` |
| `tools/codex-supervisor/test_supervisor.py` | `ea55e7da74b17c875ee4c7012592e60de94199969121ca9567f1925cb0e3395a` |
| `tools/codex-supervisor/milestones.json` | `98c84645ebeda1df3ab96b975036ec86fe13556881bb7c4285f85d29cc73aa57` |
| `tools/codex-supervisor/README.md` | `a7562655b9f4ecd8ac9c82c0686726f83bca867dda209361182168c6082edf50` |
| `tools/codex-supervisor/__pycache__/supervisor.cpython-313.pyc` | `5e550c2ce0577c301aa545987ab1cce88cd4eda9075697d7a5b08535e4b9301c` |
| `tools/codex-supervisor/__pycache__/test_supervisor.cpython-313.pyc` | `1fe6457ab93666bb05688ba768dca99e518cc59834aa876293b439948e056507` |
| Questo rapporto | calcolato dopo la stesura; riportato nel messaggio finale perché il digest è auto-referenziale. |

## Stato Git, baseline e proposta Sessione 68

Branch iniziale e finale: `main`. `HEAD` iniziale e finale: `abd6bac4b368427b76f6c98522afde928b418939`; nessun commit o push. Il worktree iniziale conteneva modifiche tracciate a CMake/test e numerosi elementi non tracciati, inclusi `.codex-supervisor/`, `.codex/`, directory build, cache e rapporti di qualificazione. Sono stati preservati. I test usano directory temporanee isolate e non hanno modificato lo stato o i log preesistenti in `.codex-supervisor/`.

Sessione 68 proposta: su un host Debian usa-e-getta e approvato, eseguire manualmente il solo `preflight` contro il percorso assoluto del CLI installato e verificare directory e credenziali ambientali. Se il preflight è valido, sottoporre a review un nuovo piano sintetico monomilestone con directory di stato/temp dedicate e limiti ridotti; l'eventuale prova del CLI deve essere singola e approvata esplicitamente. Non avviare automaticamente Sessione 68 e non abilitarne catene. Nessuna attività privilegiata o Alpha M1 operativa va inclusa.

**Alpha M1 resta BLOCKED.**
