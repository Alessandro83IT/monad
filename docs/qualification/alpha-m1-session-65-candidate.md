# Alpha M1 — Session 65 candidate

**Supervisore: PASS sintetico con condizioni. Qualificazione reale Codex: BLOCKED / NOT RUN. Alpha M1: BLOCKED.** La Sessione 65 ha verificato il supervisore in Debian 13, ma non ha qualificato un'esecuzione Codex annidata: il comando `codex` non è installato o disponibile nel `PATH`.

## Architettura e modifiche

Il supervisore esaminato è `tools/codex-supervisor/supervisor.py`. Legge il piano JSON in `tools/codex-supervisor/milestones.json`, vincola l'autorizzazione al digest del piano, registra stato e log sotto `.codex-supervisor/`, avvia una sola milestone per invocazione e confronta gli snapshot Git prima/dopo. I test sintetici usano un CLI fittizio e verificano successi e fallimenti senza avviare Codex.

In questa sessione non è stato modificato codice, piano, README, baseline storica o file amministrativo. Non è stato autorizzato o preparato un piano temporaneo modificando quello preesistente: il suo ambito ammette modifiche al codice del supervisore e al rapporto Sessione 64, in contrasto con il requisito di una prova senza modifiche operative. Senza CLI disponibile, preparare o autorizzare una prova reale non avrebbe prodotto una qualificazione verificabile.

## Ambiente e CLI

- Sistema: Debian GNU/Linux 13 (trixie), versione `13.7`.
- Python: `3.13.5`.
- `command -v codex` e `type -a codex`: nessun eseguibile trovato. Il percorso vendor Codex presente nella variabile `PATH` non contiene un comando risolvibile.
- `codex --version` e `codex --help`: entrambi non eseguibili (`command not found`). Versione 0.164.0 e parser delle opzioni: **NOT RUN**.
- Sessione Codex reale tramite supervisore: **NOT RUN / BLOCKED**, nessun avvio, exit code, log o rapporto di run attribuibile a Codex. Non classificata PASS.
- Non sono stati installati software né cercati/attivati binari alternativi.

Il codice invoca `codex exec --approve-for-me --output-last-message <file> <prompt>` senza un'opzione esplicita di sandbox. Non è stato possibile verificare i default del CLI annidato o che la configurazione effettiva mantenga la sandbox richiesta. L'assenza di un flag di bypass non dimostra da sola la sandbox effettiva. Questo è un blocco per una prova reale, non un motivo per aggirare la restrizione.

## Test e verifiche

| Verifica | Esito | Dettaglio |
|---|---|---|
| Test sintetici supervisore | **PASS** | `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/codex-supervisor -p 'test_*.py' -v`; 28 test, 9.741 s, exit 0. |
| Errori/timeout della suite | **FAIL 0; TIMEOUT 0** | Nessun test fallito o scaduto. |
| CLI Codex 0.164.0 e opzioni | **NOT RUN / BLOCKED** | Eseguibile assente. |
| Ciclo reale: avvio, exit code, log e rapporto Codex | **NOT RUN** | Nessuna sessione reale avviata. |
| Codice, collector, diagnostica, test privilegiati, sudo | **SKIP / NON ESEGUITI** | Fuori perimetro; nessun collector reale o storico eseguito. |
| D6, baseline storica, digest promossi | **SKIP / NON ACCEDUTI** | Nessun accesso o aggiornamento. |
| CTest/CMake Monad | **NOT RUN** | Modifiche esaminate limitate al supervisore Python; nessun build avviato. |

La suite include verifiche di exit non-zero, rapporto mancante, test obbligatori falliti o inventati, timeout, stop, discendenti, cambio di branch/piano, modifiche protette o fuori ambito, contesa lock e recovery ambiguo.

## Perimetro, integrità e recovery

Il supervisore non è stato lanciato sul repository reale, quindi non esiste un esito di conformità del perimetro prodotto da un run. Durante la sessione sono stati eseguiti soltanto letture dello stato/documenti e i test sintetici in directory temporanee; il test suite non ha creato `.codex-supervisor/` nel repository. L'integrità del piano autorizzato non è stata messa alla prova in una sessione reale; i test includono invalidazione quando cambiano i byte del piano e modifica del piano durante un run.

La recovery è conservativa: `resume` rifiuta run in stato `RUNNING` o con gruppo residuo e non rilancia processi ambigui. I test sintetici coprono questo rifiuto e la terminazione/registrazione dei discendenti. Rimangono limiti dichiarati: stato e lock locali non autenticati, richieste stop cooperative, cleanup limitato al gruppo di processi e possibile fuga con `setsid()`.

Lo snapshot Git non costituisce isolamento: ignora file ignorati e percorsi fuori repository, non rileva modifiche transitorie poi ripristinate, e rappresenta i symlink senza digestarne il target. Le allowlist non confinano Codex. L'attuale milestone inoltre consente scritture nei sorgenti del supervisore; un futuro probe di solo ciclo di vita richiede un piano temporaneo esplicito e ristretto a fixture usa-e-getta, prima di ogni autorizzazione.

## Review indipendenti

- **Core — nessuna correzione proposta.** Il ciclo sintetico registra `RUNNING` prima del processo; exit non-zero, output assente, test falliti, timeout, stop, modifiche fuori ambito e cambio identità repository impediscono PASS. Recovery non rilancia run ambigui. Senza CLI non si può dichiarare PASS reale. Ha rilevato che `run` controlla `RUNNING`, mentre `resume` controlla anche il gruppo residuo; lo stato manipolato manualmente rientra nel limite same-UID documentato.
- **Security — nessuna correzione applicata; condizioni bloccanti documentate.** La sandbox annidata non è esplicitata né verificabile qui. Gli snapshot non coprono file ignorati/esterni e target symlink. Il piano corrente è più ampio del probe Sessione 65. La pulizia processi non prova containment contro escape di gruppo. Non raccomanda un run reale in questo ambiente.
- **Operations — baseline ed evidenze preservate.** Conferma CLI assente, run reale NOT RUN, recovery non rilanciante. Non raccomanda di modificare/autorizzare il piano corrente in assenza del CLI. Alpha M1 resta BLOCKED.

## Stato Git, baseline e artefatti

Branch `main`; `HEAD` iniziale e finale `abd6bac4b368427b76f6c98522afde928b418939`. Al controllo iniziale il worktree era già ampiamente modificato: modifiche tracciate a CMake/test e molti artefatti non tracciati, report, directory build e cache. Sono stati preservati. L'unico nuovo file di questa sessione è questo rapporto candidato. Nessun commit o push.

SHA-256, calcolati sui byte finali dei file esistenti (riferimenti d'integrità locali, non pin promossi):

| Artefatto | SHA-256 |
|---|---|
| `tools/codex-supervisor/supervisor.py` | `60cefdfad541cc4b1bab3fd424da7aaff3f55d6df2b5a91d2ddb1b5824c9be68` |
| `tools/codex-supervisor/milestones.json` | `d3a40f7b68f1d22ab9a2d8e9bd94cea1daebe5f1219ae94afe186892cabce391` |
| `tools/codex-supervisor/test_supervisor.py` | `3ddeb6c58cd5ccdcf5c6f74b5eef0d1d1e8eb203c01a9d323be9a00e84e889b2` |
| `tools/codex-supervisor/README.md` | `5c5c9d53593fb808ebffbcaa73779ae172e4643f6771d09a7f118dee8fbbb44a` |
| `docs/qualification/alpha-m1-session-64-candidate.md` | `0780f11dd2a464cc335f851ff7a607395ac3e38d186a361b2f0e35d42e76ea9e` |
| `docs/qualification/alpha-m1-session-65-candidate.md` | calcolato dopo la stesura; self-referenziale e riportato nel messaggio di completamento |

## Limiti e proposta Sessione 66

Il risultato qualifica soltanto il comportamento sintetico del supervisore e i suoi test presenti. Non qualifica CLI 0.164.0, opzioni, sandbox annidata, registrazione di un run reale o perimetro filesystem completo. Non sono state apportate correzioni perché il blocco ambientale impedisce di validare il problema di compatibilità; i review non hanno identificato una correzione sicura che possa sostituire questa prova.

Proposta per Sessione 66: ripetere una singola prova di ciclo, senza catene, solo quando Codex CLI 0.164.0 è già disponibile nell'ambiente Debian autorizzato; verificare versione e opzioni effettive, definire e rivedere un piano temporaneo che consenta esclusivamente output usa-e-getta, e stabilire esplicitamente sandbox e approvazioni senza indebolirle. Se i controlli ambientali bloccano il run, registrarne il blocco senza aggiramenti. Nessuna attività Alpha M1 operativa va inclusa.

**Stato Alpha M1: BLOCKED.**
