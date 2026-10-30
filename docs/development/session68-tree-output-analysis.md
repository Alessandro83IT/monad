# Analisi del fallimento del collaudo Sessione 68

## Esito

Il collaudo ha terminato il processo Codex con `exit_code=0`, ma il supervisore ha correttamente classificato la run come `UNQUALIFIED`: `output_limit_exceeded=true` e `unsafe_outputs=true`. Non sono rimasti processi e la verifica della run ha riportato zero modifiche al repository. La credenziale staged risulta rimossa secondo il rapporto conservato nella directory di evidenza.

## Causa

La scansione conservata in `/tmp/monad-session68.nPayytIN/state/runs/a0615e65-94ef-458a-8a85-2afdd251319f/CODEX_HOME` mostra tre link simbolici temporanei creati dal runtime Codex sotto `tmp/arg0-linux/codex-arg0pwzcvp/`: `codex`, `applypatch` e `apply_patch`. Puntano tutti al binario Linux del runtime Codex installato. Il controllo in `tree_output_bytes` non segue link non autorizzati: li classifica come output non sicuro. Il campionamento live ha quindi impostato `output_limit_exceeded` e terminato la run anche se il CLI ha poi restituito zero.

Il totale finale di 3.013.809 byte include anche i database SQLite e i relativi file WAL/SHM in `CODEX_HOME`; la soglia dichiarata nel piano era 16.777.216 byte, quindi il totale da solo non supera il limite. I file ordinari e i file speciali non sono stati confusi con i symlink: WAL/SHM sono file regolari conteggiati per dimensione. Gli altri artefatti non regolari restano motivo di rifiuto.

## Valutazione e modifica

Non è emerso un difetto nel rifiuto di output non previsto: approvare link in base al solo nome o consentire tutti i link sotto `CODEX_HOME/tmp` indebolirebbe il controllo, perché un processo potrebbe sostituirli o indirizzarli altrove. I link osservati sono plausibili artefatti del runtime, ma il loro target non era autorizzato esplicitamente dal supervisore. Non è stata aggiunta alcuna eccezione di allowlist e il collaudo host non è stato ripetuto.

Sono stati aggiunti test di regressione per garantire che un symlink non autorizzato verso un file esterno non venga seguito né conteggiato, che una FIFO sia rifiutata come file speciale e che un symlink esplicitamente autorizzato resti non seguito e non conteggiato. Il controllo di produzione non è stato modificato.

## Verifiche

- **PASS:** suite `test_supervisor.py`, 54 test, eseguita con ambiente pulito (`env -i`); include i tre nuovi casi.
- **FAIL iniziale, poi corretto:** prima esecuzione contaminata da variabili `MONAD_*`/Codex ereditate dall’ambiente host; non attribuibile alle modifiche. Il rerun isolato è passato integralmente.
- **NOT RUN:** collaudo Sessione 68 host; nessuna nuova invocazione Codex.
- **NOT RUN:** CTest e qualificazione Alpha M1; fuori ambito.

## Evidenza e perimetro

Sono stati letti solo il rapporto finale, i campi di stato della run e i metadati dei file nella directory di evidenza. Non sono stati aperti contenuti di credenziali. Evidenza esistente, build directory e modifiche preesistenti al repository sono state preservate. Questa analisi ha aggiunto solo test e questo rapporto; nessun commit o push è stato eseguito.
