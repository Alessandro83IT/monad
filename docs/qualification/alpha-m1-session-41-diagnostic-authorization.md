# Modello di autorizzazione — singola acquisizione diagnostica Alpha M1

Questo modello è una richiesta vuota, non un'autorizzazione. Va compilato e approvato da un operatore umano responsabile dopo i preflight host e guest. Un avvio di Codex, un hash valido o un precedente report non costituiscono approvazione. L'approvazione vale solo per una singola acquisizione diagnostica non-root e non autorizza staging, canary, test, installazione, rollback o cleanup.

## Identità e artefatti

- Operatore richiedente / identificativo: `____________________________`
- Responsabile approvatore / identificativo e ruolo: `____________________________`
- Data/ora UTC e finestra approvata: `____________________________`
- Host VirtualBox / hostname e identificativo: `____________________________`
- Nome VM / UUID: `____________________________`
- Stato VM: `____________________________`
- Snapshot o punto di ripristino / UUID: `____________________________`
- Guest Debian / machine-id / boot-id / ora UTC: `____________________________`
- Evidenza del collegamento host↔guest e riferimenti ai transcript: `____________________________`
- Checkout esatto, branch e HEAD: `____________________________`
- Runner SHA-256 completo approvato: `468756ff8cc1f55e09e4560ec1e47974b53a076219715fa35bf36874fb2cc618` oppure nuovo valore ricalcolato e revisionato: `____________________________`
- Metodo che garantisce che l'interprete esegua proprio i byte runner approvati, senza sostituzione dopo l'hash: `____________________________`
- Collector SHA-256 completo approvato: `6527d34cbfcca75244a77a1457f13e3d8e1b79aee5393f348c73bc96d74a74d2`
- Verificatore storico: 149 file, SHA-256 `f229f1b367f31053a283f564b3d2fa3edd713bfa1ede5134c899466f1bb8f67f`, risultato e ora: `____________________________`

## Scope preciso

- Read set completo del collector, controllato voce per voce e approvato: `____________________________`
- Path evidence parent preesistente, owner/mode/ACL/mount verificati: `____________________________`
- Path capture directory preesistente, owner/mode 0700, ACL/mount verificati: `____________________________`
- Nomi nuovi riservati per stdout, stderr, status e record; tutti assenti al preflight: `____________________________`
- Proprietario della conservazione e modalità trasferimento output: `____________________________`
- Utente guest previsto; esplicitamente non-root: `____________________________`
- Numero di acquisizioni autorizzate: **una**.

Operazioni consentite esclusivamente da questa approvazione:

- [ ] Verificare hash runner/collector e storico con comandi read-only approvati.
- [ ] Avviare una singola esecuzione del runner per il collector fissato, come utente guest non-root, sugli input/read set elencati sopra.
- [ ] Scrivere esclusivamente gli output nuovi nominati sopra nelle due destinazioni private nominate.
- [ ] Conservare e rivedere stdout, stderr, exit status, record e output collector senza alterazioni.

Operazioni vietate:

- Avviare/arrestare la VM o creare/eliminare/ripristinare snapshot.
- Eseguire runner o collector come root o con privilegi elevati.
- Eseguire CTest/test binari, canary, installer, staging, installazione, rollback o cleanup.
- Modificare servizi/systemd, cgroup, mount, permessi, ownership, ACL o contenuti storici/D6/fixture.
- Scrivere fuori dai path output approvati, sovrascrivere nomi esistenti o ripetere l'acquisizione.
- Allargare o selezionare diversamente il read set durante l'esecuzione.

## Condizioni di arresto

Fermare la procedura senza retry e preservare gli output parziali se si verifica una delle seguenti condizioni:

1. UUID VM, snapshot, machine-id, boot-id, kernel, checkout o finestra temporale discordanti o non verificabili.
2. Hash runner/collector non corrispondente all'approvazione, runner eseguibile da pathname non protetto da sostituzione, seal mancanti o verifier storico diverso da 149/digest atteso.
3. Symlink, file speciale, hardlink inatteso, owner/mode/ACL/mount o tipo file inattesi, input inaccessibile o read set non esaminato.
4. Directory output mancante, non privata, non preesistente, non approvata, con path component symlink o con un nome output già esistente.
5. Qualsiasi richiesta di privilegi, errore di apertura/lettura, errore o status incompleto, segnale, timeout, record/output parziale, output non attribuibile o processo figlio che non termina.
6. Comparsa di cambiamenti in systemd/cgroup/mount, contenuti D6/fixture, checkout o corpus storico.

In caso di stop: non correggere permessi, non eliminare file, non riprovare, non terminare processi e non proseguire verso staging/installazione. Conservare lo stato osservato, registrare l'ora/status e chiedere una nuova review umana.

## Decisione

- Decisione: `[ ] APPROVO una sola acquisizione diagnostica  [ ] NEGO  [ ] RINVIO`
- Motivazione e riferimenti alle prove: `____________________________`
- Firma/registrazione approvatore: `____________________________`
- Data/ora UTC: `____________________________`
- Condizioni aggiuntive (non ampliano automaticamente lo scope): `____________________________`

L'approvazione compilata non equivale a una decisione di qualificazione Alpha M1. Tutti i gate amministrativi successivi richiedono decisioni separate.
