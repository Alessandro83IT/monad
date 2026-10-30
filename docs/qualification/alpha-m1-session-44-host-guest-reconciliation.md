# Session 44 — Checklist read-only host VirtualBox ↔ guest Debian

Eseguire le query sull'host VirtualBox autorizzato e poi dalla console guest già autorizzata. Nessun comando qui avvia/arresta/modifica la VM. Salvare i risultati solo nel transcript controllato dell'operatore; in questa sessione non viene scritto nulla sulla VM. `VBoxManage` potrebbe non esistere nel container: il container non sostituisce l'host.

## Host VirtualBox

```sh
date -u --iso-8601=seconds
hostnamectl status
VBoxManage --version
VBoxManage list vms
VM_UUID='e475ea91-72e9-4157-9ee6-583e9ae0462f'
VBoxManage showvminfo "$VM_UUID" --machinereadable
VBoxManage snapshot "$VM_UUID" list --details
# Ripetere read-only per gli UUID dei dischi identificati nei controller:
VBoxManage showmediuminfo DISK_UUID
```

Confermare host/operatore, nome e UUID VM, `VMState` stabile, snapshot corrente UUID/nome, configurazione/dischi e timestamp. Non usare `guestcontrol ... run`; non avviare/arrestare VM né cambiare snapshot o medium. Snapshot corrente non prova assenza di modifiche guest successive.

## Guest — stessa console VM, sessione operatore attestata

Aprire/identificare in VirtualBox Manager la VM dal suo UUID e usare la console già associata a quella VM. L'operatore deve attestare che i comandi seguenti sono stati eseguiti in quella console, senza sessione SSH non riconciliata. Raccogliere transcript e timestamp UTC coordinati con la verifica host.

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
findmnt -rn -o TARGET,SOURCE,FSTYPE,OPTIONS
df -hT
```

Il nome host non è prova d'identità. `machine-id` individua l'installazione guest e `boot-id` il boot corrente; da soli non legano una VM UUID o uno snapshot. Registrare il product UUID DMI se leggibile e confrontarlo solo tramite mapping firmware/VirtualBox già documentato per questa VM; non supporre che il suo formato debba eguagliare l'UUID VM. Un mismatch o mapping ignoto lascia G6 BLOCKED.

## Riconciliazione

Il record di riconciliazione deve contenere: host e operatore; UUID VM e snapshot corrente osservati direttamente con VBoxManage; indicazione dell'operatore che la console VirtualBox mostrata appartiene a quella UUID; transcript guest dalla stessa console con timestamp, machine-id, boot-id e DMI IDs; riferimenti incrociati e risultati grezzi. I due UUID e la console devono essere legati nella stessa sessione controllata, non raccolti come report indipendenti. Il metodo è un binding operativo attestato dall'operatore, non una firma crittografica prodotta dagli strumenti. Se l'operatore non può attestare il contesto console o la relazione resta ambigua, G6 resta BLOCKED.

Un boot-id non certifica la snapshot di origine. L'operatore deve inoltre attestare che la snapshot dichiarata è il punto autorizzato e registrare la storia/continuità del boot; una snapshot presente ma non corrente o una VM in transizione implica stop. Non ripristinare snapshot per risolvere una discordanza.

## Pin e output — fasi separate con review

Prima verificare disponibilità del runtime e permessi senza privilegi. Per ogni path, una fase per volta: `namei -l PATH`; review; `stat -c '%F %a %u:%g %n' PATH`; review; `sha256sum PATH` solo dopo esito positivo. Per ACL/mount: `getfacl -p PATH`, `findmnt -T PATH -o TARGET,SOURCE,FSTYPE,OPTIONS`. Non proseguire dopo symlink, parent inatteso o accesso negato.

Baseline completa da verificare sul guest:

- Bootstrap `3a5aa58d0a87b1363e61180773b4fd0318b930d41b81186a39ff0fd40a71f9a6`
- Runner `f67a5dde11add6ced5802fa8091680f75f480a43fcc386f2049844f313b2f469`
- Collector `6527d34cbfcca75244a77a1457f13e3d8e1b79aee5393f348c73bc96d74a74d2`
- Verificatore storico: 149 file, `f229f1b367f31053a283f564b3d2fa3edd713bfa1ede5134c899466f1bb8f67f`
- Inline loader (estratto dalla procedura e ricontrollato byte per byte): `feef002511128263daf5fa05fcaa72686a688d58fb717b2f8d1a9d026b205021`

Verificare inoltre i sei hash Sessione 34B elencati nel report Sessione 43 e tutti i file/procedure approvati. Non modificare né aprire dati D6 oltre all'inventario names-only autorizzato. Non eseguire il collector nel preflight.

Il parent collector e la capture directory devono essere due directory già esistenti, private, canoniche e fuori da checkout, corpus storico, fixture, D6 e path amministrativi. Verificare owner, mode, ACL, mount, componenti e nomi nuovi assenti. Capture directory mode 0700; parent collector posseduto dall'utente guest previsto e non group/world writable. Non creare, riparare o scrivere su nessuna directory durante il preflight. Le informazioni in Sessione 43 sul path candidato restano riferite finché non si produce questo controllo.

## Disposizione

- [ ] Host/VM/snapshot corrispondenti e stabili.
- [ ] Console guest legata all'UUID host nella stessa sessione controllata.
- [ ] Machine-id, boot-id, firmware/DMI, OS/kernel e ora registrati.
- [ ] Checkout, pin completi, corpus e read set verificati.
- [ ] Parent output e capture directory esistenti e conformi; nomi assenti.
- [ ] Nessun comando privilegiato, test, runner, collector o scrittura effettuati.

Qualsiasi checkbox mancante o prova non autenticabile mantiene G6 o i gate di output/runtime **BLOCKED**. Questo controllo non autorizza l'acquisizione.
