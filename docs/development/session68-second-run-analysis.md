# Analisi del secondo collaudo Sessione 68

## Sintesi

Il secondo collaudo in `/tmp/monad-session68.XZuVOKHZ` è terminato con CLI `exit_code=0`, ma correttamente `UNQUALIFIED`: `unsafe_outputs=true`, `output_limit_exceeded=true`, `output_bytes=3013809` e `tests=[]`. La causa è dimostrata: la validazione introdotta per `arg0-linux` descriveva un singolo link in `XDG_RUNTIME_DIR/arg0-linux` diretto al launcher `codex.js`; il runtime reale crea invece una directory temporanea di link sotto `CODEX_HOME/tmp/arg0-linux/codex-arg…`, diretti al payload Linux bundled `codex`. Il monitor live vedeva quindi link non autorizzati e interrompeva il processo. Il codice di controllo ha operato fail-closed come progettato.

## Confronto delle evidenze

Le run precedenti `nPayytIN` e `XZuVOKHZ` riportano entrambe `exit_code=0`, `output_bytes=3013809`, `output_limit_exceeded=true`, `unsafe_outputs=true`, `tests=[]`, `status=UNQUALIFIED`, nessun processo residuo e zero modifiche al repository. Entrambe hanno 67 file regolari, 26 directory e 3 symlink nel `CODEX_HOME`; le dimensioni e le destinazioni dei link coincidono. Cambia soltanto il suffisso casuale della directory `codex-arg…`. La seconda run presenta gli stessi tre link osservati nella prima: `apply_patch`, `applypatch`, `codex-linux-sandbox`, tutti con payload letterale identico diretto al binario bundled. Non sono state lette credenziali.

La somma dei byte è verificata: i file regolari di `CODEX_HOME` totalizzano 3.013.673 byte. `codex.log` aggiunge 48 byte e `report.txt` 88, per un totale esatto di 3.013.809. Il limite dichiarato nel piano è 16.777.216 byte, dunque la soglia numerica non è stata superata. I WAL e SHM SQLite sono file regolari: vengono conteggiati per dimensione. Non sono presenti file speciali o ulteriori link nel `CODEX_HOME`. `tests=[]` è conseguenza del terminamento live: il ciclo dei test richiesti non parte dopo `output_limit_exceeded`.

## Correzione

La validazione ora ammette soltanto link diretti con uno dei quattro nomi runtime documentati (`codex`, `applypatch`, `apply_patch`, `codex-linux-sandbox`), collocati esattamente due livelli sotto `CODEX_HOME/tmp/arg0-linux` in una directory `codex-arg…`, e con target letterale uguale al payload Linux bundled accanto al launcher selezionato. Il target viene derivato per architettura dalle sole posizioni previste del pacchetto Codex e deve essere un file regolare eseguibile. Se il payload non è identificabile, nessun link viene ammesso. Il controllo usa `lstat`/`readlink`: non risolve né legge il target. I link non conformi, le directory/link aggiuntivi, i nomi diversi e i file speciali continuano a fallire chiusi. Il budget dei file regolari non è stato modificato.

Sono stati aggiunti test per il layout transitorio reale, per il rifiuto di un target diverso e per posizione/nome/target errati. La documentazione del supervisore riflette ora il contratto verificato. Il vecchio rapporto relativo alla prima run è stato preservato.

## Verifiche

- **PASS:** 3 test mirati sui symlink arg0, eseguiti con ambiente ridotto (`env -i`).
- **PASS:** intera suite sintetica `python3 -m unittest discover -s tools/codex-supervisor -p 'test_*.py'`, 62 test, con `PATH`, `HOME` espliciti e `PYTHONDONTWRITEBYTECODE=1`; durata 14,263 s.
- **NOT RUN:** nuovo collaudo host Sessione 68, invocazione Codex, test privilegiati, CTest, Alpha M1.
- **NON ESEGUITI:** commit e push.

Le directory d’evidenza esistenti, i build e le modifiche preesistenti sono stati conservati. La correzione e la suite sintetica non costituiscono qualificazione Alpha M1.
