# Alpha M1 — Session 66 candidate

**Supervisore: PASS sintetico con condizioni. Probe Codex annidato: BLOCKED. Alpha M1: BLOCKED.** Il CLI installato è stato verificato come versione 0.164.0. È stato effettuato un solo tentativo annidato, con sandbox workspace-write e revisione automatica; l'app-server non si è inizializzato nel filesystem in sola lettura. Nessun bypass o retry è stato tentato.

## Modifiche

- `tools/codex-supervisor/supervisor.py`: `run --codex-bin PATH` e `MONAD_CODEX_BIN` selezionano l'eseguibile senza modificare `PATH`; ogni invocazione ne verifica la versione esatta 0.164.0. `--plan-file PATH` e `MONAD_CODEX_PLAN_FILE` selezionano un piano alternativo, sempre vincolato al digest registrato. I subprocess dei test ricevono `PYTHONDONTWRITEBYTECODE=1` per evitare di alterare cache preesistenti.
- `tools/codex-supervisor/test_supervisor.py`: aggiunti casi per eseguibile assoluto, file mancante, permessi di esecuzione assenti, codice di uscita non-zero dalla verifica versione e `PATH` ridotto. Il test di versione errata già esistente resta attivo.
- `tools/codex-supervisor/README.md`: documentati override di eseguibile e piano, limiti del controllo versione e risultato runtime della Sessione 66.
- Questo rapporto.

Il piano predefinito `tools/codex-supervisor/milestones.json` non è stato modificato né autorizzato. Il piano temporaneo esterno al repository era `/tmp/monad-session66-plan-koi5sz2v.json`, con una sola milestone `session66-noop-probe`, timeout 300 secondi e prompt che vietava strumenti, comandi e modifiche al repository. Il suo SHA-256 è `51940105c8fe45962b89ff891493f375d83f75400b7d800b3711f54f5fe7d3b3`.

## Verifica e test

| Verifica | Esito | Dettaglio |
|---|---|---|
| CLI Codex via percorso assoluto | **PASS** | `/home/alessandro/.local/npm/bin/codex --version` → `codex-cli 0.164.0`, exit 0. È apparso anche l’avviso non fatale che non poteva creare alias PATH per filesystem in sola lettura. |
| Sandbox/revisione automatica dichiarate dal CLI | **PASS** | `codex exec --help` indica che `--approve-for-me` instrada le richieste alla revisione automatica con sandbox `workspace-write`. L’argv del supervisore include tale opzione e non include flag di bypass di sandbox o hook trust. |
| Suite sintetica isolata | **PASS — 33 test** | `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/codex-supervisor -p 'test_*.py' -v`; 33 test, exit 0. FAIL 0, TIMEOUT 0. |
| Suite del tentativo supervisionato | **FAIL — 4 test** | Il comando richiesto dal piano ha completato 33 test ma 4 casi di successo del CLI sintetico sono falliti: `test_absolute_binary_works_with_reduced_path`, `test_absolute_configured_binary_is_used`, `test_only_explicitly_authorized_next_milestone_can_advance`, `test_success`. Exit 1. Gli altri 29 sono passati. Nessun retry annidato è stato eseguito. |
| Probe Codex annidato | **BLOCKED** | Unico run ID `281854c5-9978-4e02-b018-f9102705191e`; il supervisore ha restituito exit 2 e stato `BLOCKED`. Il processo CLI ha exit 1 e nel log riporta: `Error: failed to initialize in-process app-server client: Read-only file system (os error 30)`. Non è stato prodotto il rapporto finale Codex (`report_present=false`). |
| Run supervisore: perimetro e recovery | **BLOCKED / conservativo** | Nessuna modifica protetta, branch o `HEAD`; nessun gruppo di processi residuo. Il supervisore ha segnalato come fuori ambito le due cache bytecode indicate sotto. Lo stato e i log del run sono stati conservati. |
| Build/CTest Monad | **NOT RUN** | Non necessario per le modifiche Python. |
| Sudo, installazione, collector, D6, diagnostica reale, test privilegiati, promozione digest, commit/push | **SKIP / NON ESEGUITI** | Restano fuori dal mandato della sessione. |

### Cache preesistenti modificate dal tentativo

Prima del run, il rapporto Sessione 64 registrava questi digest per cache già esistenti. Il comando di test interno del tentativo le ha ricompilate senza `PYTHONDONTWRITEBYTECODE`; il supervisore ha correttamente bloccato il run per queste variazioni fuori ambito. Non sono state cancellate o ripristinate perché non è disponibile una copia dei byte originali. La suite isolata successiva non le ha riscritte. La modifica del supervisore ora imposta `PYTHONDONTWRITEBYTECODE=1` per i test futuri.

| Cache | SHA-256 iniziale registrato in Sessione 64 | SHA-256 dopo il tentativo |
|---|---|---|
| `tools/codex-supervisor/__pycache__/supervisor.cpython-313.pyc` | `f8d80b85c552a98f1bd2ac983b585b611b23e0727f79546a17e3cc0257dcb5cf` | `5e550c2ce0577c301aa545987ab1cce88cd4eda9075697d7a5b08535e4b9301c` |
| `tools/codex-supervisor/__pycache__/test_supervisor.cpython-313.pyc` | `0e3208cf594cb7a54917db0892cfd615f5d01ed6297dd66178ff07b5e7d17dc1` | `1fe6457ab93666bb05688ba768dca99e518cc59834aa876293b439948e056507` |

## Esecuzione annidata e artefatti

Il run è stato lanciato una sola volta con:

```text
python3 tools/codex-supervisor/supervisor.py --plan-file /tmp/monad-session66-plan-koi5sz2v.json run session66-noop-probe --timeout 300 --codex-bin /home/alessandro/.local/npm/bin/codex
```

Il log completo del CLI è `.codex-supervisor/runs/281854c5-9978-4e02-b018-f9102705191e/codex.log`. L’output del comando test interno è in `test-1.log` nella stessa directory. Il file `report.txt` non esiste perché l’app-server non ha prodotto un messaggio finale. Gli artefatti non sono stati rimossi.

SHA-256 degli artefatti finali (riferimenti locali, non pin promossi):

| Artefatto | SHA-256 |
|---|---|
| `tools/codex-supervisor/supervisor.py` | `ae8d8ea29e1440644b34c383d6277b0dcda01680f4d1faacfff6b27c9f312d1a` |
| `tools/codex-supervisor/test_supervisor.py` | `2bc9ed8e7208b6a0039a39d897f4c969b05883399a78fe9e583d55ba6f2c8dcd` |
| `tools/codex-supervisor/README.md` | `b86f8f16e7fd4315b1551bf2215e2344110a35ba32ebb3a12ab683bac23535f6` |
| `tools/codex-supervisor/milestones.json` (immutato) | `d3a40f7b68f1d22ab9a2d8e9bd94cea1daebe5f1219ae94afe186892cabce391` |
| Piano temporaneo | `51940105c8fe45962b89ff891493f375d83f75400b7d800b3711f54f5fe7d3b3` |
| Log Codex | `eae080369e91b8869ecdd955b8e2a9840b501bc2c8dfb0889bae645cc549d4b5` |
| Log test del run | `589ce9db2f5644f804a1f78a6d70a824da41094605170f70c201c24e9a8d61d2` |
| `alpha-m1-session-66-candidate.md` | digest riportato nel messaggio finale, dopo la stesura |

## Stato Git e baseline

Branch `main`; `HEAD` iniziale e finale `abd6bac4b368427b76f6c98522afde928b418939`. Le modifiche tracciate, i report, gli artefatti, le cache e le directory build preesistenti sono stati preservati. Il run ha aggiunto `.codex-supervisor/` con stato e log; il supervisore inizialmente non esisteva. Le due cache bytecode sono state modificate come descritto sopra. Non risultano altre variazioni del run nel suo snapshot. Nessun commit o push.

## Review indipendenti

- **Core — PASS WITH CONDITIONS.** L’override assoluto non dipende dal `PATH`, mantiene la verifica versione e la selezione del piano alternativo resta vincolata al digest. I test coprono i casi richiesti. La review ha ritenuto ragionevole il solo probe no-op con timeout e controllo dello stato prima/dopo.
- **Security — PASS WITH CONDITIONS, non una qualifica di sicurezza.** `--approve-for-me` usa revisione automatica con sandbox `workspace-write`; nessun flag pericoloso è stato usato. Il supervisore non è un confine di contenimento: non rileva file ignorati/esterni o modifiche transitorie; lo stato locale è mutabile dallo stesso UID; il binario è verificato per versione ma non autenticato tramite digest. La review ha chiesto una sola esecuzione e nessun retry dopo ambiguità.
- **Operations — PASS WITH CONDITIONS.** Il piano Sessione 64 è troppo ampio e non è stato usato. Log e stato del tentativo sono stati mantenuti; nessuna recovery automatica o ripartenza. La review conferma di non ritentare se il run è bloccato e di mantenere Alpha M1 BLOCKED.

## Limiti residui e proposta Sessione 67

La prova reale non ha raggiunto il prompt del modello: il CLI 0.164.0 non ha inizializzato l’app-server a causa del filesystem in sola lettura. Non usare questa evidenza per dichiarare un run Codex riuscito o una qualifica Alpha M1. Il processo di test interno ha inoltre esposto che l’esecuzione non isolata della suite può toccare cache bytecode; la nuova impostazione evita la scrittura nei run futuri, ma non ripristina i byte originali delle cache già modificate.

Proposta Sessione 67: investigare, senza avviare sessioni automaticamente, un ambiente runtime Codex scrivibile e usa-e-getta approvato esplicitamente, mantenendo configurazione, trust degli hook, revisione automatica e sandbox workspace-write. Prima di un eventuale singolo probe, verificare che l’app-server possa inizializzarsi e che il preflight non richieda bypass o modifica di controlli; usare un piano monomilestone nuovo e registrare digest e output. Se il preflight resta bloccato, registrarlo senza retry o workaround. Non avviare la Sessione 67 né catene automatiche.

**Alpha M1 resta BLOCKED.**
