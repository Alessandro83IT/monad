# Monad Alpha M1 — Sessione 39: runner verificato e preparazione acquisizione

**Stato finale: BLOCKED. Installazione NON AUTORIZZATA. Alpha M1 NON qualificata.** Nessun collector reale è stato eseguito. Nessun CTest Alpha, comando privilegiato, installazione, canary, rollback, cleanup o operazione VM è stato eseguito. Durata effettiva stimata: circa 35 minuti.

## Preflight

- Ambiente: container Debian GNU/Linux 13.7, kernel `6.12.113+deb13-amd64`, x86_64, `systemd-detect-virt=container-other`, uid/gid 1000 (`alessandro`). Non è l'host VirtualBox né la VM di qualificazione; `VBoxManage` non disponibile. Nessuna identità VM/snapshot è stata osservata.
- Repository: `/home/alessandro/Projects/monad`, branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`, come previsto. Il working tree era già ampiamente modificato: nove file tracked modificati e numerosi file untracked, build e rapporti. Questi elementi sono stati preservati; questa sessione ha creato solo i file elencati sotto.
- Rapporti Sessions 35–38 e relative procedure/record sono presenti e leggibili. Sono artefatti locali untracked senza baseline firmata in questo checkout; la loro presenza non è autenticità esterna.
- Il collector esistente ha SHA-256 `6527d34cbfcca75244a77a1457f13e3d8e1b79aee5393f348c73bc96d74a74d2`, uguale al pin richiesto. Il suo contenuto non è stato modificato.
- La directory storica indicata dalla Sessione 38 è accessibile e contiene 149 file. La Sessione 38 registra digest atteso `f229f1b367f31053a283f564b3d2fa3edd713bfa1ede5134c899466f1bb8f67f`. Un'aggregazione locale tentata in Sessione 39 con `sha256sum` sulle righe dei file ha prodotto `4eb691cd353b983d296fdba8f862c2041253662e727461605b0084afcfdbc1a4`; il metodo di aggregazione non è descritto nel record precedente, quindi questo risultato non è direttamente confrontabile e l'integrità aggregata storica non è stata riconfermata. Nessun file storico è stato scritto o modificato. Per la regola di stop, non avviare una raccolta finché la discrepanza metodologica non è risolta con il metodo canonico documentato.
- Strumenti presenti: Python 3, `sha256sum`, `stat`, `readlink`, `realpath`, `flock`, CMake, CTest e Ninja. `shellcheck` e `VBoxManage` non disponibili.

## Runner e test

Creati:

- `qualification/alpha-m1-verified-evidence-runner.py`
- `qualification/alpha-m1-verified-evidence-runner.md`
- `tests/alpha_m1_verified_evidence_runner_test.py`
- `qualification/alpha-m1-session-39-acquisition-procedure.md`
- questo rapporto.

Il runner è legato al collector fisso e al digest incorporato. Apre il collector senza seguire un symlink finale, richiede un file regolare con un solo hard link, copia e hasha i byte aperti in un `memfd`, verifica il pin e applica/verifica i seal Linux di scrittura/crescita/troncamento. In modalità esplicita `--execute`, Bash legge il solo descriptor sigillato; la sostituzione successiva del pathname non sostituisce i byte eseguiti. L'ambiente del processo è allowlistato. La modalità predefinita verifica soltanto. Timestamp, digest, wait status, segnale catturato e provenienza locale sono emessi come JSON su stderr dopo la terminazione. Limiti e failure modes sono descritti nella documentazione; in particolare, SIGKILL, crash, perdita di alimentazione o mancata conservazione di stderr possono impedire la persistenza del record.

SHA-256 runner: `0151e13f7f6bf391ba6609e33cf7e2ba6d442faf5938a926edc333ac36d05891`.

Esecuzione test finale: `python3 -m unittest -v tests.alpha_m1_verified_evidence_runner_test` — **13 PASS, 0 FAIL, 0 SKIP**. Le fixture sono sintetiche e verificano pin/seal, digest errato, file assente e non leggibile, symlink/hardlink, sostituzione pathname post-snapshot, mutazione durante la copia, errori di lettura, interruzione con status registrato e terminazione dei processi figli, fallimento del collector e integrità output, isolamento da `BASH_ENV`/funzioni shell, CLI verification-only e rifiuto di argomenti collector arbitrari. Non viene invocato il collector reale.

Preflight del runner senza opzioni: digest corretto, `verification_only=true`; nessuna esecuzione. Non è stata fornita l'opzione `--execute`.

## Provenienza dei build locali

Esaminati in sola lettura `build-monad-debug` e `build-monad-release`; nessuna ricompilazione, esecuzione binari o CTest.

| Build | Cache e generatore | Artefatto osservato | SHA-256 | Valutazione |
|---|---|---|---|---|
| `build-monad-debug` | source `/home/alessandro/Projects/monad`, Ninja, Debug, `/usr/bin/c++`; metadata compiler GNU 14.2.0 | `mpkg`, 17,224 byte | `60b58e37eb687d4d25df09e0e3e37e1524f4fd289c7a3600b6ab7c76dceae7f0` | Provenienza parziale: percorso e configurazione osservati; nessun legame autenticato sorgenti-processo-output |
| `build-monad-release` | source `/home/alessandro/Projects/monad`, Ninja, Release, `/usr/bin/c++`; metadata compiler GNU 14.2.0 | `mpkg`, 15,960 byte | `4c51b175cb08072c990d2c1d4c72f767a98a24742c088891b167bfcdbadf0bf1` | Provenienza parziale: percorso e configurazione osservati; nessun legame autenticato sorgenti-processo-output |

Entrambi i build sono sotto checkout con working tree sporco. Cache, generator files e digest binario non dimostrano da soli che quegli input abbiano prodotto i binari. Provenienza sorgente-binario: **non verificabile**; i path VM previsti (`/opt/athena-qualification/build` e `/tmp/monad-s32-verification-final4-20261021`) non sono stati letti né identificati come questi build locali.

## Revisioni indipendenti

- **Core:** revisione finale completata; giudicato corretto il disegno sealed-memfd e fixed path. Le revisioni hanno portato a inoltro segnali al process group, copertura dei figli e ripristino handler anche su errori di spawn. Rimangono i limiti per arresti non gestibili e possibile mancata persistenza del record in caso di guasto del canale di cattura.
- **Security:** revisione finale completata sul runner SHA-256 `0151e13f7f6bf391ba6609e33cf7e2ba6d442faf5938a926edc333ac36d05891`. Il rischio iniziale di `BASH_ENV` e funzioni esportate è chiuso con ambiente allowlistato e test sintetico. Il forwarding al process group copre i processi figli. Nessun blocker residuo sul vincolo byte verificati/eseguiti; il runner stesso, kernel, `/proc` e `/bin/bash` restano nel confine di fiducia.
- **Verification:** revisione finale e rerun completati: 13/13 PASS. Copertura negativa e fixture-only ritenute adeguate allo scope; il caso unreadable è esercitato nel contesto uid 1000.
- **Operations:** revisione della procedura completata; mantiene approvazione registrata, identità VM/snapshot, utente ordinario, output parent preesistente, cattura no-clobber, letture protette sequenziali, preservazione D6/storico e divieto di CTest/installazione. L'esecuzione resta non autorizzata.

## Blocchi residui e decisione

- Non disponibile ambiente host/VM autenticato, né UUID/snapshot; nessuna acquisizione può essere fatta qui.
- Metodo canonico del digest storico non reperito: la ricomputazione tentata non è confrontabile con il pin riportato. Nessun collector deve essere eseguito finché questo punto non è chiarito.
- Provenienza dei build non verificabile; il preflight locale non sostituisce l'ispezione dei due percorsi previsti né un'attestazione.
- I 34 controlli storici BLOCKED e i 7 NOT RUN restano invariati.
- L'opzione `--execute` è un interruttore esplicito, non un sistema di autorizzazione. Serve una distinta approvazione umana che identifichi VM, snapshot, checkout, scope, destinazione e modalità di conservazione.

Stato Alpha M1: **BLOCKED**. Installazione: **NON AUTORIZZATA**.
