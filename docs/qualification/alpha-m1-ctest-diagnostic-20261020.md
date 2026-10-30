# Athena Alpha M1 — Diagnosi CTest (2026-10-20)

## Esito

La suite non privilegiata è stata eseguita da una build Debug pulita del commit
Alpha M1 identificato sotto. Esito CTest: **111 PASS, 11 SKIP, 0 FAIL, 0
TIMEOUT** su 122 test selezionati. Gli altri 18 test, marcati `privileged`, non
sono stati eseguiti. Alpha M1 **non è qualificata**: mancano le evidenze della
parte privilegiata richieste dalla qualificazione.

## Sorgente, branch e build

- Il checkout `/home/alessandro/Projects/athena-pm` non è più presente. Ho
  recuperato il repository dall’archivio locale
  `athena-pre-monad-20261020-115435.tar.gz`, SHA-256
  `a2105468208dd1b2e3e8ff406b2d62940781206af03d47580837b4ff9cebb71f`.
- Checkout estratto: branch `fix/alpha-m1-umask`, HEAD
  `4b2d4719665fbcbb16b6c68b0ee2d0914c01fe6f`. Non risultano modifiche tracked;
  l’archivio contiene già directory non tracciate `build-alpha/`,
  `docs/qualification/alpha-m1-privileged-report.md` e `qualification/`.
- Il `build-alpha/` conservato nel workspace Monad è inutilizzabile come build
  corrente: il suo `CMAKE_HOME_DIRECTORY` e i comandi CTest puntano al checkout
  scomparso `/home/alessandro/Projects/athena-pm`. Non l’ho modificato.
- Build diagnostica nuova: `/tmp/alpha-m1-diagnostic-20261020/build-debug`,
  Ninja, Debug, GCC 14.2.0, CMake 3.31.6. Configurazione e compilazione da zero
  riuscite (280 step). È una build riproducibile dal commit archiviato, ma non è
  la build autenticata originale.
- Ambiente: UID 1000, `CapEff=0`. Nessun comando privilegiato o `sudo` è stato
  usato.

## Baseline storica 109/28/3

La baseline 109 PASS / 28 SKIP / 3 FAIL è stata fornita nella richiesta
precedente. Non ho trovato un log completo che la documenti: il `LastTest.log`
del vecchio `build-alpha` è vuoto e `LastTestsFailed.log` contiene soltanto
`initial_activation_migration_test`. Le tre cause seguenti sono quelle
osservate nella precedente esecuzione della build Monad Release; non vanno
attribuite automaticamente al checkout Alpha M1 senza distinguere il ramo:

| Test | Diagnosi storica |
|---|---|
| `packaging_regression_test` | Fixture e asserzioni conservavano i nomi Athena (`athena`, `AthenaPM.AppDir`), mentre la build Monad installava `mpkg` in `MonadPM.AppDir`. Disallineamento della migrazione di nome; il test equivalente passa nel checkout Alpha M1 qui eseguito. |
| `qualification_driver_test` | L’inventario della build Monad richiedeva `packaging/AppDir/athena-pm.desktop` e `athena.png`, ma i file presenti erano `monad-pm.desktop` e `monad.png`. Inventario non aggiornato alla rinomina; il test passa nel checkout Alpha M1 qui eseguito. |
| `initial_activation_migration_test` | Errore esplicito: impossibile stabilire l’assenza di xattr nascosti `trusted.*` senza `CAP_SYS_ADMIN` nell’namespace iniziale. È una limitazione di ambiente/privilegi; non ho ripetuto il test perché è marcato `privileged`. |

Le correzioni alle fixture Monad erano già presenti come modifiche non
committate nel workspace Monad all’inizio di questo turno. In questo turno non
ho cambiato i sorgenti Alpha estratti dall’archivio.

## Esecuzione osservata direttamente

Comando applicato dalla diagnostica: CTest Release/Debug coerente con la build,
`-LE privileged`, 2 worker, timeout CTest predefinito 90 s, timeout per test
esplicito conservato dalle proprietà CMake, limite globale 900 s. I test con
proprietà propria possono quindi avere un limite superiore al default; i test
di fault-boundary e `activation_selection_test` hanno limite 600 s.

| Test lungo | Risultato | Durata |
|---|---:|---:|
| `activation_selection_test` | PASS | 234,82 s |
| `activation_plan_persistence_boundaries_fresh_test` | PASS | 93,11 s |
| `activation_plan_persistence_boundaries_bootstrap_test` | PASS | 91,36 s |
| `activation_plan_persistence_boundaries_reused_test` | PASS | 81,29 s |
| `activation_plan_persistence_boundaries_published_test` | PASS | 55,82 s |

I test lenti non erano bloccati: hanno completato il controllo dei rispettivi
boundary. Il run finale con `-C Debug` è terminato in 359,38 s, prima del limite globale, con
exit code CTest 0. Non si sono verificati FAIL o TIMEOUT.

`packaging_regression_test` (CTest #90) è PASS, 4,32 s, exit code 0;
`qualification_driver_test` (#139) è PASS, 0,33 s, exit code 0.
`initial_activation_migration_test` (#134) è **NOT RUN**, come richiesto.

Gli 11 SKIP sono i test di integrazione/scanner che richiedono la verifica di
xattr `trusted.*`; CTest li ha classificati con `SKIP_RETURN_CODE=77`. Li ho
lasciati SKIP. I restanti 18 test col label `privileged` sono **NOT RUN** e
compaiono singolarmente nel TSV/JSON con durata e codice non applicabili.

I risultati per tutti i 140 test, inclusi i 18 esclusi, sono registrati in
`qualification/alpha-m1-diagnostic-20261020/athena-debug-final/parsed/20261020T114849Z/results.tsv`
e `results.json`.

- Log CTest, JUnit e console del run finale: `qualification/alpha-m1-diagnostic-20261020/athena-debug-final/20261020T114229Z/`

Per PASS il codice del processo è 0; per gli SKIP è 77, secondo la proprietà
CTest configurata. Per FAIL/TIMEOUT/non eseguiti il codice è lasciato vuoto,
perché non si è osservato un exit code del test. Il TSV riporta anche il timeout
individuale applicabile a ciascun test.

Il primo run aveva generato tutti gli esiti, ma il parser del prototipo
diagnostico non riconosceva il padding a punti delle righe CTest e terminava
dopo il test run. Ho corretto il parser e completato il run finale qui
riportato; i suoi log e il riepilogo sono persistenti.

## Test non eseguiti e qualificazione

Non sono stati eseguiti i 18 test etichettati `privileged`, incluso
`initial_activation_migration_test`, perché l’ambiente non dispone di
`CAP_SYS_ADMIN` e la richiesta vieta di eseguirli o aggirarne gli SKIP. Questo
run verifica quindi la build e la suite non privilegiata del commit Alpha M1,
ma non la migrazione iniziale né le prove di mount/owner/ext4 richieste per
Alpha M1. Non dichiaro la qualificazione completata.

## Prossimi passi

1. Portare il checkout autenticato del commit
   `4b2d4719665fbcbb16b6c68b0ee2d0914c01fe6f` nella VM Debian prevista; non usare
   il `build-alpha` con percorsi CMake ormai stale.
2. Eseguire lì lo script non privilegiato allegato, se serve ripetere questa
   matrice: `qualification/diagnose-alpha-m1-unprivileged.py`. Registra per
   test risultato, durata, exit code interpretato da CTest, timeout, XML JUnit
   e log persistenti; esclude sempre il label `privileged` e non invoca sudo.
3. Ottenere separatamente l’autorizzazione operativa e i prerequisiti della VM
   per la matrice privilegiata prevista dal protocollo Alpha M1, quindi
   registrarne tutti gli esiti senza convertire errori o condizioni mancanti in
   PASS/SKIP artificiali.
4. Conservare commit, cache/build identity, log completi, stato dei fixture e
   verifiche di cleanup; confrontare i risultati con la baseline 109/28/3 solo
   dopo aver recuperato i log originari.
