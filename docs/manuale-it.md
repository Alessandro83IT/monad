# Monad Package Manager
## Manuale utente e guida per collaboratori

Edizione iniziale in italiano · 31 ottobre 2026 · Baseline documentata: `0.1.0-alpha.1`

**Stato: documentazione della versione sperimentale.** Questo manuale descrive i sorgenti forniti negli archivi di revisione del 30 ottobre 2026. Non certifica una release, la qualificazione Alpha M1 o il comportamento di revisioni successive. I comandi sono stati confrontati con il codice; in questa sessione non sono stati eseguiti perché CMake non è disponibile.

## Indice

1. [Che cos’è Monad](#1-che-cosè-monad)
2. [Funzionalità e limiti](#2-funzionalità-e-limiti)
3. [Concetti fondamentali](#3-concetti-fondamentali)
4. [Compilazione](#4-compilazione)
5. [Primo avvio](#5-primo-avvio)
6. [Configurazione e percorsi](#6-configurazione-e-percorsi)
7. [Primo pacchetto locale](#7-primo-pacchetto-locale)
8. [Riferimento dei comandi](#8-riferimento-dei-comandi)
9. [Generazioni e rollback](#9-generazioni-e-rollback)
10. [Integrità e modello di esecuzione](#10-integrità-e-modello-di-esecuzione)
11. [Problemi frequenti](#11-problemi-frequenti)
12. [Guida per collaboratori](#12-guida-per-collaboratori)
13. [Mappa della documentazione tecnica](#13-mappa-della-documentazione-tecnica)
14. [Fonti e verifica editoriale](#14-fonti-e-verifica-editoriale)

## 1. Che cos’è Monad

Monad Package Manager è un package manager sperimentale per Linux, scritto in C++20. Il programma da terminale si chiama `mpkg`. Il progetto sviluppa un percorso che collega metadati del pacchetto, risoluzione delle dipendenze, compilazione dei sorgenti, archiviazione degli artefatti e attivazione di generazioni.

La direzione del progetto combina compilazione da sorgenti ispirata a Gentoo/Portage, configurazione dichiarativa ispirata a Nix, storage immutabile e generazioni. L’integrazione con l’ecosistema Arch è un obiettivo. Queste aspirazioni non costituiscono una dichiarazione di compatibilità o completezza nella versione descritta.

Monad è il package manager su cui si basa il progetto Henosix OS. Questo manuale riguarda Monad: non documenta un installer di Henosix, un sistema avviabile completo o una procedura di amministrazione della distribuzione.

Nei namespace C++, nei file e in alcuni messaggi compare ancora il nome precedente `athena`. Per usare la CLI descritta qui, il nome pubblico è `mpkg`. Non rinominare manualmente lo stato persistente per adeguarlo al nuovo nome.

## 2. Funzionalità e limiti

| Area | Stato nella baseline documentata |
|---|---|
| Catalogo locale | Lettura di manifest TOML sotto `repositories/state/packages` |
| Dipendenze | Risoluzione tramite modello pubblico e piano di installazione |
| Sorgenti eseguibili | Una sola sorgente locale `file:///percorso/assoluto` |
| Archivio | Sottoinsieme ristretto di tar compresso gzip, con una directory principale |
| Build | CMake, senza parametri di build aggiuntivi nella ricetta eseguita |
| Digest della sorgente | Verifica SHA-256 se presente nei metadati |
| Storage moderno | Oggetti indirizzati dal digest del contenuto installato |
| Stato installato | Record persistenti con informazioni semantiche delle dipendenze |
| Generazioni | Elenco, lettura corrente, switch e rollback nella CLI |
| Rimozione | Validazione dell’ambiente risultante prima dell’attivazione |
| Garbage collection | Meccanismi interni; nessun comando pubblico `mpkg gc` |
| Sorgenti remote, ricette multi-sorgente | Non supportate dal percorso attuale di realizzazione |
| Autotools, import binari, esecuzione Athena IR | Non supportati dal percorso attuale di realizzazione |
| Sandbox e build riproducibili | Nessuna garanzia fornita dall’esecutore attuale |
| Sostituzione di pacman, emerge o Nix | Non pronta per uso affidabile in produzione |

La presenza di un tipo, di un backend storico o di un modulo nel codice non significa che la CLI lo esponga o che il percorso moderno lo esegua. Un risultato di test isolato non equivale alla qualificazione dell’intero prodotto.

## 3. Concetti fondamentali

**Pacchetto e candidato.** Il pacchetto ha un nome e una versione; un candidato conserva anche l’identità del repository e del relativo snapshot. Due selezioni con lo stesso nome visibile non sono necessariamente la stessa selezione completa.

**Ricetta e piano.** La ricetta descrive come ottenere e costruire una sorgente. Il resolver sceglie i candidati e prepara un piano; la realizzazione consuma le selezioni del piano. Non deve sostituirle con una nuova scelta dal catalogo durante la build.

**Artefatto.** È il risultato installato in staging, analizzato secondo lo schema degli artefatti. Il suo `ArtifactId` usa SHA-256 e descrive l’output normalizzato. Il digest dell’archivio sorgente descrive invece i byte dell’archivio: i due digest hanno scopi distinti.

**ObjectStore.** Lo store moderno conserva gli oggetti sotto `objects/v1/sha256/<digest>`, relativamente alla radice dello store configurata. Oggetti identici possono essere condivisi da più record. Non modificare direttamente file e metadati dello store.

**Realizzazione.** Un record collega una selezione precisa all’artefatto verificato e conserva le informazioni necessarie sullo stato installato. Le build ambientali attuali non producono una prova di build riproducibile né un’identità completa degli input di compilazione.

**Generazione.** Una generazione registra un insieme di appartenenze dei pacchetti. L’ID numerico della generazione è distinto dall’identità del candidato e dal digest dell’artefatto.

**Attivazione.** Il sistema proietta gli output nel target configurato, mediante link e directory. Il target della prova è una directory sotto la radice sperimentale. La sua attivazione non aggiunge automaticamente programmi al `PATH` della shell.

## 4. Compilazione

Servono Linux, un compilatore C++20, CMake almeno 3.20 secondo il requisito dichiarato, Ninja oppure Make, librerie di sviluppo OpenSSL e LibArchive, pthreads e Python 3 per controlli del parser e del packaging. Per i test di packaging servono anche `readelf`, `ldd` e `mktemp`.

Da un checkout completo del progetto, usare una directory di build nuova:

```bash
cmake -S . -B build-manual -DCMAKE_BUILD_TYPE=Release
cmake --build build-manual --parallel
./build-manual/mpkg --version
./build-manual/mpkg --help
```

Il target CMake interno dell’eseguibile conserva il nome `athena`, mentre il file prodotto si chiama `mpkg`. Il numero dichiarato dalla baseline è `0.1.0-alpha.1`.

Per compilare anche i test e avviare quelli privi dell’etichetta privilegiata:

```bash
ctest --test-dir build-manual --output-on-failure -LE privileged
```

Un test non etichettato come privilegiato può comunque saltare parte della copertura per limiti di ispezione del filesystem. Registrare separatamente PASS, FAIL, SKIP, TIMEOUT e NOT RUN; non contare uno SKIP come un PASS.

Per provare l’installazione dei file del programma in un prefisso utente dedicato, dopo la build:

```bash
cmake --install build-manual --prefix "$HOME/monad-manual-prefix"
"$HOME/monad-manual-prefix/bin/mpkg" --version
```

Scegliere un prefisso nuovo e non condiviso con altre installazioni. La relazione relativa tra directory binari e librerie deve essere conservata. Questo passaggio installa il programma Monad; è diverso da `mpkg install`, che gestisce i pacchetti nel suo ambiente.

## 5. Primo avvio

Eseguire le prove come utente normale, in una radice nuova e sacrificabile. `--root` sposta lo stato del package manager e il target; non isola il codice di build.

Dalla stessa shell e dalla radice del checkout:

```bash
trial_root="$(mktemp -d /tmp/monad-manual-XXXXXXXX)"
./build-manual/mpkg --root "$trial_root" config check
./build-manual/mpkg --root "$trial_root" paths
./build-manual/mpkg --root "$trial_root" list
./build-manual/mpkg --root "$trial_root" generation list
```

`paths` è un comando diagnostico presente nel codice, ma non elencato nell’help generale. Una radice vuota può non contenere alcuna generazione inizializzata: il messaggio corrispondente è atteso.

Conservare il valore di `trial_root` per il tutorial successivo. Alla chiusura della shell, la variabile non viene conservata automaticamente. Non sostituirla con `/` o con una directory di sistema. Non usare `sudo` per far riuscire una prova che fallisce.

## 6. Configurazione e percorsi

La precedenza di caricamento, dalla più bassa alla più alta, è: impostazioni predefinite, configurazione di sistema, configurazione utente, file esplicito, override della riga di comando.

| Origine | Percorso o selezione |
|---|---|
| Configurazione di sistema | `/etc/athena/athena.toml` |
| Configurazione utente con XDG | `$XDG_CONFIG_HOME/athena/config.toml` |
| Configurazione utente alternativa | `$HOME/.config/athena/config.toml` |
| File esplicito | `--config /percorso/config.toml` |
| Radice esplicita | `--root /percorso/assoluto` |
| Radice predefinita storica | `/var/lib/athena` |

Per le prove usare sempre una radice esplicita. Un file minimo di configurazione può essere:

```toml
schema_version = 1
[paths]
root = "/tmp/monad-prova-dedicata"
```

Sostituire il percorso con quello scelto. Validazione:

```bash
./build-manual/mpkg --config /percorso/assoluto/config.toml config check
```

Il file esplicito non esclude il caricamento precedente dei file di sistema e utente: un errore in uno di essi può bloccare anche questa prova. I percorsi configurati devono essere assoluti. `config check` controlla la configurazione e mostra la provenienza dei valori; non prova che un’installazione funzionerà o che il filesystem soddisfi i requisiti dello scanner.

Con il solo `--root R`, i percorsi derivati sono:

| Contenuto | Percorso |
|---|---|
| Store | `R/store` |
| Stato generale | `R/state` |
| Cache | `R/cache` |
| Workspace di build | `R/build` |
| Snapshot repository | `R/repositories/snapshots` |
| Stato repository | `R/repositories/state` |
| Manifest catalogo | `R/repositories/state/packages/NOME/package.toml` |
| Generazioni | `R/generations` |
| Target di attivazione | `R/target` |

La configurazione consente anche override specifici dei percorsi. Per capire la disposizione effettiva usare `paths`. L’opzione `--root` ricalcola i percorsi derivati nel percorso CLI attuale: non presumere che conservi override di directory caricati dal file.

## 7. Primo pacchetto locale

Questo esempio prepara una piccola sorgente leggibile che installa un file di testo. È derivato dalla struttura dei test di integrazione, ma non è stato eseguito durante questa redazione. I comandi di installazione possono essere bloccati dai requisiti dello scanner descritti al capitolo 11.

Prerequisiti: avere completato la build e definito `trial_root` come al capitolo 5. Eseguire dalla radice del checkout, nella stessa shell. La preparazione seguente richiede Bash, tar e sha256sum.

```bash
example_root="$(mktemp -d /tmp/monad-example-XXXXXXXX)"
mkdir -p "$example_root/hello-1"
cat > "$example_root/hello-1/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(monad_hello NONE)
install(FILES hello.txt DESTINATION share/monad-hello)
CMAKE
printf '%s\n' 'Ciao da Monad!' > "$example_root/hello-1/hello.txt"
tar -czf "$example_root/hello-1.tar.gz" -C "$example_root" hello-1
archive_digest="$(sha256sum "$example_root/hello-1.tar.gz")"
archive_digest="${archive_digest%% *}"
mkdir -p "$trial_root/repositories/state/packages/hello"
cat > "$trial_root/repositories/state/packages/hello/package.toml" <<TOML
name = "hello"
version = "1"
source = "file://$example_root/hello-1.tar.gz"
build_system = "cmake"
sha256 = "$archive_digest"
dependencies = []
TOML
```

Il backend installa con prefisso `/usr`, quindi il file finale comparirà sotto `target/usr/share/monad-hello`. L’espansione di `file://` davanti al percorso assoluto produce `file:///tmp/...`. La directory temporanea usa un percorso semplice, senza spazi o caratteri sensibili alla shell. L’archivio ha una sola directory principale. Il campo `build_system` è esplicito: il backend predefinito storico può essere Autotools, che il percorso di realizzazione attuale non supporta.

Il manifest risultante ha questa forma:

```toml
name = "hello"
version = "1"
source = "file:///tmp/PERCORSO-ESEMPIO/hello-1.tar.gz"
build_system = "cmake"
sha256 = "DIGEST-SHA256-DELL-ARCHIVIO"
dependencies = []
```

Quest’ultimo blocco è illustrativo: i segnaposto devono essere sostituiti. Il blocco Bash precedente lo fa automaticamente.

Avviare l’installazione e fermarsi se restituisce un errore:

```bash
./build-manual/mpkg --root "$trial_root" install hello
```

Solo dopo un esito positivo, controllare:

```bash
./build-manual/mpkg --root "$trial_root" list
./build-manual/mpkg --root "$trial_root" info hello
./build-manual/mpkg --root "$trial_root" generation current
cat "$trial_root/target/usr/share/monad-hello/hello.txt"
```

Il contenuto atteso dell’ultimo file è `Ciao da Monad!`. `info` espone nome, versione, repository e identità dell’artefatto per le appartenenze moderne. Per rimuovere il pacchetto nella sola radice di prova:

```bash
./build-manual/mpkg --root "$trial_root" remove hello
./build-manual/mpkg --root "$trial_root" list
./build-manual/mpkg --root "$trial_root" generation list
```

La rimozione modifica l’ambiente attivo, ma non implica la cancellazione immediata degli artefatti conservati dalle generazioni storiche. Lasciare le directory di prova disponibili finché servono per capire l’esito; questo tutorial non include pulizia automatica.

Per introdurre una dipendenza semplice, un manifest può usare `dependencies = ["nome-dipendenza"]`, con il corrispondente pacchetto nel catalogo locale. Il modello interno gestisce espressioni più ricche; la grammatica completa va consultata nei sorgenti del parser e nei test prima di scrivere nuove ricette complesse.

## 8. Riferimento dei comandi

Negli esempi seguenti `mpkg` indica il binario compilato o installato. Aggiungere sempre `--root "$trial_root"` nelle prove. I nomi in maiuscolo sono segnaposto.

| Sintassi | Effetto |
|---|---|
| `mpkg --help` oppure `mpkg -h` | Mostra l’aiuto generale |
| `mpkg --version` oppure `mpkg -V` | Mostra la versione |
| `mpkg --root R config check` | Valida la configurazione |
| `mpkg --root R paths` | Mostra i percorsi effettivi; comando diagnostico |
| `mpkg --root R install NOME` | Risolve e installa un singolo nome richiesto, con le dipendenze selezionate |
| `mpkg --root R remove NOME [ALTRO...]` | Rimuove uno o più pacchetti se il risultato è semanticamente valido |
| `mpkg --root R list` | Mostra i pacchetti della generazione corrente |
| `mpkg --root R info NOME` | Mostra informazioni di un pacchetto installato |
| `mpkg --root R generation list` | Elenca le generazioni, segnando quella corrente |
| `mpkg --root R generation current` | Mostra la generazione corrente e le appartenenze |
| `mpkg --root R generation switch ID` | Attiva una generazione conservata specificata |
| `mpkg --root R generation rollback` | Sceglie la generazione conservata con massimo ID inferiore al corrente |

Le opzioni globali `--config PATH` e `--root PATH` richiedono un valore. Per l’help generale usare `mpkg --help`: il parser non offre un sistema uniforme di help contestuale per ogni sottocomando. Messaggi che riportano ancora `athena` sono residui della rinomina.

Non sono documentati come comandi pubblici `update`, `upgrade`, `search`, `gc`, un gestore di repository remoti o una modalità `--dry-run`. Nel codice esistono comandi diagnostici e di test: non sono il percorso di uso ordinario.

## 9. Generazioni e rollback

Una nuova installazione o rimozione riuscita può creare una generazione e attivarla. Le generazioni conservate mantengono riferimenti agli oggetti necessari. Controllare sempre `generation list` e `generation current` prima di una transizione.

```bash
mpkg --root "$trial_root" generation list
mpkg --root "$trial_root" generation current
# Sostituire 1 con un ID effettivamente presente e appropriato:
mpkg --root "$trial_root" generation switch 1
```

`rollback` segue l’ordine numerico, non la cronologia delle ultime attivazioni. Se gli ID conservati sono 0, 2 e 5 e il corrente è 5, sceglie 2. Se il corrente è 2, sceglie 0 anche se prima era stato attivato 5. Se non esiste un ID inferiore disponibile, segnala un errore.

Lo switch verifica gli oggetti moderni e valida lo stato semantico pertinente prima di modificare il target. Un oggetto mancante o corrotto può impedirlo anche se i metadati della generazione esistono.

Il rollback riguarda l’insieme di pacchetti e la loro proiezione nel target configurato. Non è un backup completo, non riporta indietro documenti personali o database applicativi e non promette un ripristino atomico dell’intero filesystem dopo un’interruzione di alimentazione. Directory di supporto possono restare dopo la disattivazione.

Gli ID non sono digest del contenuto né identificatori globali perpetui: l’allocazione dipende dalle generazioni conservate. Nei report registrare anche la revisione del codice e le identità degli artefatti.

## 10. Integrità e modello di esecuzione

Il percorso moderno è: catalogo locale → risoluzione → piano → acquisizione locale → eventuale verifica del digest sorgente → estrazione privata → CMake → staging → scansione dell’output → pubblicazione nello store → registrazione e attivazione della generazione.

La verifica di un digest prova una corrispondenza del contenuto, non l’identità o l’affidabilità dell’autore. Il sistema non introduce automaticamente firme, attestazioni del produttore o una catena completa di provenienza della build.

Gli script CMake della sorgente vengono eseguiti con i privilegi dell’utente corrente. Una radice di stato temporanea non limita il loro accesso agli altri file dell’utente o alla rete. Usare sorgenti fidate e un ambiente di prova dedicato. Non eseguire le build come root.

L’estrazione attuale rifiuta diversi casi non supportati, inclusi attraversamenti di percorso, duplicati, link e file speciali nell’archivio. Lo schema degli artefatti installati può avere capacità diverse dal sottoinsieme accettato per l’archivio sorgente.

Se una realizzazione successiva fallisce, oggetti già pubblicati possono rimanere nello store senza una nuova generazione attiva. Non cancellarli a mano. Le protezioni di ritenzione, i pin e la garbage collection sono parte del protocollo interno.

La validazione offline dello stato installato usa i metadati persistenti per controllare dipendenze, conflitti e requisiti pertinenti; non equivale a garantire che qualunque programma installato sia eseguibile o interoperabile nel sistema reale. Gli stati storici con informazioni incomplete seguono regole conservative: l’assenza di dati non viene trasformata in prova di assenza di dipendenze.

## 11. Problemi frequenti

| Problema | Verifica e azione |
|---|---|
| CMake non trovato | Installare la toolchain nell’ambiente di sviluppo; il binario non può essere compilato senza di essa |
| Header OpenSSL o LibArchive mancanti | Verificare la presenza dei pacchetti di sviluppo, oltre alle librerie runtime |
| Libreria core non trovata dopo installazione | Conservare il layout relativo bin/lib e controllare i requisiti runtime; consultare `packaging/INSTALL.md` |
| Configurazione non valida | Usare percorsi assoluti e `config check`; verificare i file caricati per precedenza |
| Catalogo mancante | Creare i manifest nel percorso `repositories/state/packages/NOME/package.toml` della radice effettiva |
| Ricetta non supportata | Usare una sola sorgente locale tar.gz, backend CMake e nessun parametro di build aggiuntivo |
| Digest della sorgente diverso | Verificare archivio e manifest; non sostituire il digest senza capire perché sono cambiati i byte |
| Rimozione rifiutata | Verificare le dipendenze e i conflitti dell’ambiente risultante; non modificare manualmente i record |
| Switch rifiutato | Controllare presenza e integrità degli oggetti, semantica del target e conflitti di percorso |
| Nessuna generazione precedente | Consultare gli ID conservati; rollback richiede un ID inferiore al corrente |
| Messaggio con nome Athena | Verificare la versione; alcuni messaggi e percorsi mantengono il nome storico |

### Limite dello scanner e attributi `trusted.*`

Una parte delle verifiche deve poter escludere attributi estesi nascosti `trusted.*`. Su filesystem e kernel che non consentono questa ispezione all’utente normale, l’operazione può fallire con una diagnostica che contiene `hidden trusted.*`. Alcuni test restituiscono 77 per segnalare uno SKIP in queste condizioni.

Non è necessariamente un errore della ricetta. Il progetto richiede una campagna separata su un ambiente idoneo: per la copertura pertinente può essere necessaria `CAP_SYS_ADMIN` nel namespace utente iniziale, oppure un filesystem supportato che riporti esplicitamente l’assenza di supporto agli xattr. Un namespace utente privato non soddisfa automaticamente il requisito.

Questo manuale non propone di concedere capability al binario, usare sudo o aggirare lo scanner. Conservare il messaggio e l’ambiente osservato e seguire le procedure di qualificazione del progetto. Un esempio correttamente preparato può dunque fermarsi prima dell’installazione riuscita.

### Segnalare un problema

Includere versione di Monad, revisione del checkout se disponibile, distribuzione/kernel/filesystem, comando esatto, diagnostica completa, configurazione dei percorsi e risultato dei test pertinenti. Usare un esempio minimo riproducibile. Rimuovere credenziali e dati personali dai log; conservare una copia originale privata delle evidenze necessarie.

## 12. Guida per collaboratori

### Orientarsi nel codice

| Percorso | Responsabilità da consultare |
|---|---|
| `include/athena/core/` | Tipi e interfacce del modello pubblico |
| `src/cli/` | Parsing e dispatch dei comandi |
| `src/core/local_repository.cpp` | Adattamento del catalogo locale al modello pubblico |
| `src/package/`, `src/dependency/` | Caricamento dei metadati e grammatica delle dipendenze |
| `src/realization/` | Record persistenti ed esecuzione della realizzazione |
| `src/artifact/` | Scansione, schema e identità degli artefatti |
| `src/store/` | ObjectStore e protocolli dello store |
| `src/install/`, `src/installed/` | Transazioni e validazione semantica installata |
| `src/generation/` | Appartenenze, persistenza e transizioni delle generazioni |
| `tests/` | Test, fixture e casi di regressione |
| `docs/adr/` | Motivazioni delle scelte architetturali |
| `docs/qualification/`, `qualification/` | Procedure e prove delle campagne di qualificazione |

Seguire il percorso di una richiesta da `src/cli/cli.cpp` è un buon punto di ingresso. Leggere prima i README dei moduli interessati e i relativi test, per distinguere il contratto pubblico da compatibilità storica e strumenti interni.

### Preparare una modifica

Da un checkout Git reale, leggere `AGENTS.md`, quindi registrare stato, branch e commit:

```bash
git status --short --branch
git branch --show-current
git rev-parse HEAD
```

Gli archivi usati per redigere questa edizione non contengono necessariamente la cronologia Git. Non presentare un hash di archivio come commit del repository. Preservare modifiche, file non tracciati ed evidenze esistenti. Usare build isolate e mantenere l’intervento circoscritto.

Le modifiche ordinarie non devono cambiare percorsi amministrativi installati, sudoers, gateway o pin di sicurezza. Tali interventi seguono il processo di revisione e autorizzazione esplicito del progetto. Non includerli implicitamente in una correzione del manuale o del codice applicativo.

### Validare una modifica

Compilare i target pertinenti e scegliere test che esercitino il comportamento modificato. Per vedere i casi registrati:

```bash
ctest --test-dir build-manual -N
```

Per eseguire i casi relativi alla CLI di installazione, dopo averne costruito i target:

```bash
ctest --test-dir build-manual --output-on-failure -LE privileged -R '^cli_install.*test$'
```

Documentare comando, configurazione Debug/Release, esiti distinti e limiti dell’ambiente. Non affermare che una campagna privilegiata sia completata sulla base dei soli test non privilegiati. La qualificazione Alpha M1 richiede l’ambiente Debian approvato e la revisione delle evidenze previste.

### Contribuire alla documentazione

Per ogni nuovo comando aggiungere sintassi, prerequisiti, effetti sullo stato, esempio e gestione degli errori. Per ogni funzionalità indicare se è obiettivo, API interna, percorso CLI o comportamento qualificato. Aggiornare questo manuale insieme al cambiamento corrispondente e registrare la revisione verificata.

Aree utili di contributo: correzione dei messaggi residui Athena, esempi verificati, copertura degli errori, test di integrazione, chiarimento dei contratti e manutenzione dell’indice. Le estensioni architetturali richiedono prima un confronto con gli ADR e i confini dei moduli esistenti.

## 13. Mappa della documentazione tecnica

I percorsi qui sotto sono relativi alla radice del repository. Questo manuale può essere aggiunto come `docs/manuale-it.md`; l’indice resta utilizzabile anche come documento autonomo.

| Documento | Quando consultarlo |
|---|---|
| `README.md` | Presentazione e stato dichiarato |
| `packaging/INSTALL.md` | Layout installato, RUNPATH, staging e packaging |
| `docs/adr/README.md` | Indice delle decisioni architetturali |
| `docs/adr/0001-build-input-artifact-identity-content-addressed-store.md` | Distinzione tra identità degli input e degli artefatti |
| `docs/artifact-schema-v1.md` | Schema degli artefatti |
| `docs/m2.2-b1-public-artifact-contract.md` | Contratto pubblico pertinente agli artefatti |
| `src/install/README.md` | Transazione di installazione |
| `src/realization/README.md` | RealizationRecord ed executor |
| `src/installed/README.md` | Validazione offline e compatibilità storica |
| `src/generation/README.md` | Formati e transizioni delle generazioni |
| `src/store/README.md` | Store e relativi confini |
| `docs/alpha-m1-legacy-layout.md` | Disposizione storica Alpha M1 |
| `docs/security/authorization-policy.md` | Regole delle operazioni sensibili |
| `docs/development/autonomous-infrastructure.md` | Infrastruttura di sviluppo autonomo |
| `docs/qualification/alpha-m1-recovery.md` | Procedure specifiche di recovery Alpha M1 |

I documenti tecnici possono descrivere singole fasi storiche. Leggerne il contesto: una garanzia di un componente non va estesa automaticamente all’intera transazione. I report di qualificazione descrivono revisioni e ambienti precisi, non certificazioni permanenti.

## 14. Fonti e verifica editoriale

Questa edizione deriva da lettura statica di README principale, `CMakeLists.txt`, modulo di packaging, `packaging/INSTALL.md`, CLI, configurazione, PathLayout, adattatore locale, README di installazione/realizzazione/generazioni/stato installato e fixture dei test CLI. Il legame con Henosix deriva dal contesto del progetto fornito dal suo autore.

Sono stati verificati nel codice: nomi dei comandi, opzioni, layout derivato, precedenza delle configurazioni, struttura minima della ricetta, scelta numerica del rollback e limiti dell’executor. Non sono stati eseguiti build, installazione, test CTest o campagna Alpha M1 durante questa redazione. Il tentativo di configurazione si è fermato perché il comando `cmake` non è presente.

Identità degli archivi forniti:

| Archivio | SHA-256 |
|---|---|
| `monad-source-review.tar.gz` | `95f446cf5aaa0e78288afe6e1598faef56ff572a4946e5c99ae3f80550705872` |
| `monad-review-extra.tar.gz` | `2eb01d8b05f4802108d320bbb11f985213f57116fe5cfa7b38d090750c866e9b` |

Gli hash identificano gli input di questa edizione, non una release approvata. Per una successiva pubblicazione collegare il manuale al commit effettivo, rieseguire gli esempi su una macchina idonea e aggiornare la sezione di verifica con risultati e limiti osservati.

Monad Package Manager: copyright © 2026 Alessandro Bifaretti. La licenza dichiarata del software è GPL-3.0-or-later; consultare `LICENSE` e `COPYRIGHT` del progetto.
