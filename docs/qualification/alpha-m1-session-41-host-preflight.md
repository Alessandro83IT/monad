# Session 41 — Checklist read-only host VirtualBox

Questa checklist va eseguita manualmente sull'host VirtualBox autorizzato, non nel container Codex. Tutti i comandi sono query. Non avviare o arrestare la VM e non creare, eliminare o ripristinare snapshot. Conservare l'output originale con timestamp UTC, identità dell'operatore e host; non scriverlo dentro il checkout o nella VM target.

I comandi `hostnamectl` e `date --iso-8601` qui sotto presuppongono un host GNU/Linux. Se l'host usa un altro sistema operativo, usare l'equivalente read-only nativo per identificare host e ora UTC; se non è disponibile, registrare BLOCKED. Non inferire host identity dal container Codex.

Impostare `VM_UUID` solo dopo aver identificato la VM dalla lista e aver confrontato il nome con l'approvazione già esistente:

```sh
date -u --iso-8601=seconds
hostnamectl status
VBoxManage --version
VBoxManage list vms
VBoxManage showvminfo "$VM_UUID" --machinereadable
VBoxManage snapshot "$VM_UUID" list --details
```

Verificare e registrare almeno:

- Nome e UUID della VM, `VMState`, `VMStateChangeTime`, `CfgFile`, tipo di firmware e controller.
- `CurrentSnapshotUUID` e `CurrentSnapshotName`, se presenti. Se assenti, snapshot corrente non dimostrato: BLOCKED finché l'operatore non identifica un altro punto di ripristino approvato.
- UUID/nome di ogni snapshot nell'albero e quale è quello corrente. L'esistenza di una snapshot non dimostra che la VM attuale non abbia modifiche successive.
- Identificativi e percorsi dei dischi collegati (campi `SATA-*`, `IDE-*`, `NVMe-*`, ecc.); verificare read-only i medium individuati:

```sh
VBoxManage showmediuminfo "$DISK_UUID"
```

Non usare comandi `startvm`, `controlvm`, `snapshot ... take/delete/restore`, `storageattach`, `modifymedium`, `guestcontrol ... run`, o comandi che scrivano proprietà. Non considerare il solo `VMState=running` prova di identità guest.

## Riconciliazione con il guest

L'operatore deve acquisire la checklist [guest Debian](alpha-m1-session-41-guest-preflight.md) da una console/sessione guest già autorizzata, senza usare `VBoxManage guestcontrol ... run`. Confrontare almeno:

1. VM UUID e stato host registrati in questo file.
2. UUID o identificativo VirtualBox esposto dal firmware guest, se disponibile.
3. `/etc/machine-id`, boot-id, orario UTC e kernel rilevati nel guest.
4. La specifica snapshot/punto di ripristino approvata e la storia del boot. Il boot-id identifica il boot corrente, non una snapshot.

Il collegamento host/guest richiede transcript controllati della stessa sessione, verificatore/operatore identificato e corrispondenza temporale; due report indipendenti non autenticati non costituiscono binding. Qualsiasi discordanza, machine-id vuoto/duplicato, snapshot non identificata o VM state transitorio comporta **BLOCKED**. Non ripristinare snapshot per risolvere la discordanza.

## Checklist esito

- [ ] Host e operatore identificati; timestamp UTC registrato.
- [ ] Nome e UUID VM corrispondono all'approvazione.
- [ ] Stato VM stabile e nessuna transizione avviata da questa procedura.
- [ ] Snapshot/punto di ripristino UUID identificato e approvato.
- [ ] Dischi e percorsi riconciliati senza modificarli.
- [ ] Risultati guest raccolti dalla VM esatta e riconciliati.
- [ ] Output del preflight conservato fuori dal checkout e reso disponibile alla review.

Un checkbox non spuntato o una prova non autenticabile mantiene il gate G6 **BLOCKED**.
