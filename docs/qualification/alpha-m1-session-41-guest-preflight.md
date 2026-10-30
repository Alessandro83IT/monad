# Session 41 — Checklist read-only guest Debian

Eseguire solo dalla console/sessione già autorizzata sulla VM target. Non eseguire questa checklist nel container Codex. I comandi riportati sono letture. Non usare `sudo`, non avviare servizi, non modificare permessi/mount, non eseguire runner, collector, CTest o binari dei build. Se un comando richiede privilegi o non è accessibile, registrare **BLOCKED**; non tentare escalation.

## Identità e sistema

```sh
date -u --iso-8601=seconds
cat /etc/os-release
uname -a
id
umask
cat /etc/machine-id
cat /proc/sys/kernel/random/boot_id
ps -p 1 -o pid=,comm=,args=
systemd --version
systemctl is-system-running
systemd-detect-virt --vm
for f in /sys/class/dmi/id/sys_vendor /sys/class/dmi/id/product_name /sys/class/dmi/id/product_uuid; do
  if test -r "$f"; then printf '%s=' "$f"; cat "$f"; else printf 'BLOCKED unreadable %s\n' "$f"; fi
done
```

Questi dati identificano il guest osservato. Non provano da soli l'UUID host VirtualBox o lo snapshot. Se `systemctl` non è disponibile o il system bus non è accessibile, registrare BLOCKED senza inferire lo stato dei servizi.

## Filesystem e mount

```sh
findmnt -rn -o TARGET,SOURCE,FSTYPE,OPTIONS
df -hT
```

Eseguire una sequenza per volta. Dopo ogni comando `namei`, fermarsi, esaminare l'output e ottenere la review richiesta prima di passare a `stat`. Fermarsi di nuovo dopo `stat` prima di esaminare il path successivo. Se un componente è symlink o inatteso, non eseguire `stat` o altri comandi su quel path.

```sh
namei -l /opt/athena-qualification/build
```

```sh
stat -c '%F %a %u:%g %n' /opt/athena-qualification/build
```

Ripetere la coppia in due comandi separati, con review dopo ogni comando, per `/tmp/monad-s32-verification-final4-20261021`, `/var/lib/athena-alpha-m1/ext4-fixture` e `/var/tmp/athena-d67-ext4`. Usare ogni volta `namei -l PATH`; solo dopo review positiva `stat -c '%F %a %u:%g %n' PATH`.

Per il residual D6 applicare lo stesso controllo in sequenza: prima `namei`, fermarsi e revisionare, quindi `stat` soltanto se i componenti sono regolari. Non leggerne o enumerarne ricorsivamente i dati:

```sh
namei -l /var/tmp/athena-d67-ext4/athena-d6-8yRDjc
```

Dopo review positiva di `namei`, eseguire:

```sh
stat -c '%F %a %u:%g %n' /var/tmp/athena-d67-ext4/athena-d6-8yRDjc
```

Non attraversare, copiare, spostare o modificare fixture/D6. Registrare symlink, owner/mode inattesi, ACL non verificabili, mount non attesi, errori di accesso o filesystem diversi come BLOCKED e fermarsi.

## Pin, output ed evidenze

Verificare solo i byte e i percorsi esplicitamente inclusi nell'autorizzazione futura. Per ogni pathname eseguire una sequenza singola: `namei -l PATH`, fermarsi e sottoporre l'output a review; se tutti i componenti sono regolari, eseguire `stat -c '%F %a %u:%g %n' PATH`, fermarsi per la review; solo dopo l'esito positivo eseguire `sha256sum PATH`. Non concatenare le fasi e non proseguire dopo symlink, tipo file, owner/mode o altra anomalia. I nomi seguenti sono baseline candidate del checkout: usare il percorso esatto approvato, senza wildcard.

Eseguire i comandi seguenti **una riga per volta** per ogni path, con review dopo ciascun comando. Il `stat` è ammesso solo dopo `namei` positivo; `sha256sum` solo dopo `stat` positivo. Non incollare l'intera tabella in una shell.

| Path suffix | First: inspect path | After review | After second review |
|---|---|---|---|
| `alpha-m1-verified-evidence-runner.py` | `namei -l /reviewed/checkout/qualification/alpha-m1-verified-evidence-runner.py` | `stat -c '%F %a %u:%g %n' /reviewed/checkout/qualification/alpha-m1-verified-evidence-runner.py` | `sha256sum /reviewed/checkout/qualification/alpha-m1-verified-evidence-runner.py` |
| `alpha-m1-vm-evidence-collector.sh` | `namei -l /reviewed/checkout/qualification/alpha-m1-vm-evidence-collector.sh` | `stat -c '%F %a %u:%g %n' /reviewed/checkout/qualification/alpha-m1-vm-evidence-collector.sh` | `sha256sum /reviewed/checkout/qualification/alpha-m1-vm-evidence-collector.sh` |
| `install-athena-alpha-m1-admin.sh` | `namei -l /reviewed/checkout/qualification/install-athena-alpha-m1-admin.sh` | `stat -c '%F %a %u:%g %n' /reviewed/checkout/qualification/install-athena-alpha-m1-admin.sh` | `sha256sum /reviewed/checkout/qualification/install-athena-alpha-m1-admin.sh` |
| `alpha-m1-build-reconciliation-session34b-candidate.json` | `namei -l /reviewed/checkout/qualification/alpha-m1-build-reconciliation-session34b-candidate.json` | `stat -c '%F %a %u:%g %n' /reviewed/checkout/qualification/alpha-m1-build-reconciliation-session34b-candidate.json` | `sha256sum /reviewed/checkout/qualification/alpha-m1-build-reconciliation-session34b-candidate.json` |
| `athena-alpha-m1-runner.py` | `namei -l /reviewed/checkout/qualification/athena-alpha-m1-runner.py` | `stat -c '%F %a %u:%g %n' /reviewed/checkout/qualification/athena-alpha-m1-runner.py` | `sha256sum /reviewed/checkout/qualification/athena-alpha-m1-runner.py` |
| `gateway_support.py` | `namei -l /reviewed/checkout/qualification/gateway_support.py` | `stat -c '%F %a %u:%g %n' /reviewed/checkout/qualification/gateway_support.py` | `sha256sum /reviewed/checkout/qualification/gateway_support.py` |
| `athena-alpha-m1-systemd-canary.py` | `namei -l /reviewed/checkout/qualification/athena-alpha-m1-systemd-canary.py` | `stat -c '%F %a %u:%g %n' /reviewed/checkout/qualification/athena-alpha-m1-systemd-canary.py` | `sha256sum /reviewed/checkout/qualification/athena-alpha-m1-systemd-canary.py` |
| `athena-alpha-m1-sudoers` | `namei -l /reviewed/checkout/qualification/athena-alpha-m1-sudoers` | `stat -c '%F %a %u:%g %n' /reviewed/checkout/qualification/athena-alpha-m1-sudoers` | `sha256sum /reviewed/checkout/qualification/athena-alpha-m1-sudoers` |

I sei artefatti 34B non autorizzano alcuna azione amministrativa: sono inclusi solo per confrontare l'identità del candidato. Rifiutare symlink e tipi diversi da file regolare dove ci si attende un file. Il normale hash per pathname non elimina da solo una race successiva; mantenere il checkout e la directory antenata sotto controllo dell'operatore durante la verifica e non trattare il digest come autenticazione indipendente.

Per l'evidence parent e il capture directory proposti, eseguire solo `namei -l`, `stat -c '%F %a %u:%g %n'`, `getfacl -p PATH` e `findmnt -T PATH -o TARGET,SOURCE,FSTYPE,OPTIONS`. Non crearli. Verificare proprietà, mode, ACL tramite strumenti già disponibili senza privilegi, mount, collocazione e assenza di nomi output preesistenti. Se `getfacl` o una verifica non è disponibile senza privilegi, il gate resta BLOCKED.

L'integrità storica locale può essere verificata in una futura sessione approvata con `python3 -I /reviewed/checkout/qualification/alpha-m1-evidence-tree-digest.py`; richiede accesso al path canonico storico e non modifica i file. Durante questo preflight non eseguire il collector e non invocare il runner con `--execute`. La modalità verification-only non esegue il collector, ma ogni suo uso richiede comunque di rispettare l'approvazione e il read scope applicabili.

## Checklist risultati guest

- [ ] Debian 13 e kernel/architettura registrati.
- [ ] VM identity firmware, machine-id, boot-id e orario registrati.
- [ ] PID 1 e systemd osservati; errori riportati senza inferenze.
- [ ] Mount/filesystem e percorsi richiesti esaminati read-only.
- [ ] Utente, gruppi e permessi effettivi registrati.
- [ ] Pin e metadata confrontati con digest completi dell'approvazione.
- [ ] Evidence parent/capture directory esistenti e proprietà/output-name checks eseguiti.
- [ ] Storico presente e digest atteso verificato, senza modifiche.
- [ ] Nessun comando di esecuzione o mutazione invocato.

Il report guest deve includere output grezzi, codice di uscita, timestamp UTC e ogni `BLOCKED`. Host e guest vanno riconciliati con la procedura host; in mancanza di binding verificabile, G6 resta BLOCKED.
