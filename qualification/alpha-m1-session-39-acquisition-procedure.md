# Alpha M1 — procedura per una successiva acquisizione autorizzata

**Bozza operativa, non autorizzazione.** La Sessione 39 non ha eseguito il collector e non ha qualificato Alpha M1.

1. Ottenere una decisione umana registrata che identifichi VM UUID, snapshot/disposable-state, operatore, finestra temporale, checkout reviewed, runner e collector digest attesi, output parent privato preesistente, modalità di conservazione/trasferimento e scope esatto di lettura. L'approvazione deve coprire esplicitamente le letture sensibili elencate nella procedura Sessione 37/38 (process arguments, journal, fixture/D6 metadata, receipts/backups/logs, generated CTest files e build metadata). Non dedurre l'identità della VM dal solo `systemd-detect-virt`.
2. Sul VirtualBox host autorizzato, identificare UUID e snapshot con la procedura Sessione 37. Non avviare, arrestare, ripristinare o modificare snapshot. Sulla VM autenticata, legare l'osservazione host a machine-id e boot-id guest.
3. In un checkout reviewed della VM, verificare branch/HEAD e integrità del runner, del collector e degli artefatti candidati rispetto ai pin approvati. Non cambiare i pin se un hash non coincide. Il runner deve provenire da una copia indipendentemente revisionata; il suo hash va annotato nell'approvazione.
4. Eseguire prima il runner senza opzioni. Confermare che mostri il digest collector atteso e `verification_only=true`. Se qualcosa differisce, fermarsi senza esecuzione.
5. Dopo approvazione esplicita della fase di raccolta e solo come utente ordinario, usare l'output parent privato, canonico e **preesistente** indicato nell'approvazione. Il collector crea un nuovo figlio univoco e rifiuta collisioni; non riutilizzare directory storiche. Predisporre inoltre una directory di cattura separata, approvata e privata, per stdout/stderr/status del wrapper. La forma seguente è solo uno schema da far includere nell'approvazione, con ogni placeholder sostituito da un percorso esatto:

   ```bash
   set -o noclobber
   exec 3> /approved/capture/runner.exit-status
   python3 -I /checkout/qualification/alpha-m1-verified-evidence-runner.py \
     --execute --output-parent /approved/evidence-parent \
     --candidate-dir /checkout/qualification \
     > /approved/capture/runner.stdout 2> /approved/capture/runner.stderr
   rc=$?
   printf '%s\n' "$rc" >&3
   exec 3>&-
   ```

   Il file di status viene riservato prima di avviare il runner; un file già esistente ferma la procedura prima dell'esecuzione. Un conflitto successivo su stdout/stderr impedisce il lancio e può far scrivere in `runner.exit-status` lo status dell'errore di redirezione shell: non è prova che il runner sia partito. Conservare l'osservazione. Lo shell deve continuare dopo un exit status non-zero affinché venga scritto il codice; non aggiungere `set -e`. Conservare tutti e tre i file e il wait status del processo supervisore. `runner.stdout` contiene sia il messaggio iniziale del wrapper sia l'output del collector; `runner.stderr` contiene il record JSON finale e gli errori. Se il processo supervisore viene terminato bruscamente e non registra lo status, mantenere l'acquisizione incompleta e BLOCKED.
6. Ispezionare `checks.tsv`, `report.md`, ogni raw output e le inventory reports. Conservare `BLOCKED`, `FAIL`, `TIMEOUT` e `NOT RUN`; non dedurre PASS da dati mancanti o errori. I controlli storici 34 BLOCKED e 7 NOT RUN non cambiano senza nuova evidenza ammissibile e revisione esplicita.
7. Per percorsi protetti, rispettare la sequenza Sessione 37: `namei` prima, revisione del risultato, approvazione puntuale del comando successivo, un comando alla volta. Fermarsi su symlink, owner/mode/ACL inattesi, errore o mount inatteso. Preservare il D6 residuo `athena-d6-8yRDjc` e tutti i 149 file storici immutati.
8. La generazione di inventory non autorizza CTest. Nessun CTest, canary, installazione, rollback o cleanup è autorizzato da questa procedura. La provenienza sorgente-binario resta separata: cache CMake, HEAD e hash artifact da soli non la dimostrano.

Il completamento della raccolta non autorizza staging o installazione. Alpha M1 rimane BLOCKED finché tutte le verifiche VM e la campagna privilegiata approvata non siano terminate e le evidenze revisionate.
