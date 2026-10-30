# Session 44 — Verified acquisition launch procedure

> **NON ESEGUIRE SENZA AUTORIZZAZIONE UMANA SPECIFICA.** Nessuna esecuzione è stata fatta in Sessione 44. Questo documento descrive la catena e una forma di comando da completare solo dopo la riconciliazione host/guest e l'approvazione di ogni path letterale.

## Valori fissati e modalità

- Inline loader SHA-256 (626 byte): `feef002511128263daf5fa05fcaa72686a688d58fb717b2f8d1a9d026b205021`
- Bootstrap SHA-256: `3a5aa58d0a87b1363e61180773b4fd0318b930d41b81186a39ff0fd40a71f9a6`
- Runner SHA-256: `f67a5dde11add6ced5802fa8091680f75f480a43fcc386f2049844f313b2f469`
- Collector SHA-256: `6527d34cbfcca75244a77a1457f13e3d8e1b79aee5393f348c73bc96d74a74d2`

Gli hash sono pin di byte e non firme. Il responsabile deve esaminarli e approvare la catena/runtime per l'ambiente VM specifico.

## Catena effettiva

1. `/usr/bin/cat` fornisce i byte del bootstrap su stdin; `env -i` limita l'ambiente del processo Python e `python3 -I -S` esclude site, user packages e import dal cwd/PYTHONPATH. Python, standard library, eseguibili, dynamic loader e kernel restano trusted.
2. Il codice inline legge massimo 262145 byte, rifiuta oltre 262144, confronta lo SHA-256 completo e compila gli stessi byte catturati. Il sorgente inline deve rimanere byte-identico alla costante `INLINE_LOADER` del test (digest sopra).
3. Gli argomenti di `python -c` sono: pin bootstrap, pin runner, path assoluto runner, poi gli argomenti runner. Il bootstrap verifica che il pin ricevuto coincida con il digest inserito dal loader, apre i componenti path senza seguire symlink, verifica file regolare/link singolo/size/stabilità/hash, quindi ricompila gli stessi byte immutabili. La path non è riaperta per caricare il codice.
4. Il runner riceve `--execute --output-parent ABS_PARENT --candidate-dir ABS_CHECKOUT/qualification`. Questa forma soddisfa parser e interfaccia fissa del collector. `--execute` da solo non è autorizzazione.
5. Il runner apre il collector fissato senza symlink finale, richiede file regolare e link singolo, lo copia/hash in un `memfd`, applica WRITE/GROW/SHRINK/SEAL, rilegge i seal e ricalcola il digest. Fallisce senza fallback se `memfd` sealing o `/proc/self/fd` mancano. Solo allora avvia `/bin/bash /proc/self/fd/N ...`, passando il fd dedicato e chiudendo gli altri. Figlio con `PATH=/usr/bin:/bin`, `LANG=C`, `LC_ALL=C`, `TZ=UTC`; niente sudo/root.
6. Il collector crea una nuova directory figlia mode 0700 nel parent già esistente. Verifica parent/candidate canonici e non symlink; il parent deve appartenere all'UID corrente e non essere group/world writable. I path sono controlli per pathname, non una difesa assoluta contro un altro processo concorrente dello stesso UID.

## Test del comando e differenze

I test Sessione 42 verificano l'hash della costante inline, il bootstrap end-to-end con payload sintetici, passaggio di stdin attraverso `subprocess.communicate`, rifiuti digest/path e API runner. Non eseguono la pipeline shell completa `cat | env python -c ...` né una acquisizione `--execute` con output capture. In Sessione 44 sono stati aggiunti controlli sintetici senza file persistenti: argomenti del bootstrap osservati identici nel runner sintetico e parser runner invocato con `--execute` mentre `snapshot_collector` e `execute_snapshot` erano mockati. Nessun collector reale è stato aperto. Questo verifica forwarding/parser, ma non sostituisce una review della shell/runtime guest.

## Comando — schema completo, ma non eseguibile finché i path non sono fissati

**Non esiste ancora un comando operativo senza placeholder.** La path assoluta del checkout nel guest, il parent output e la capture directory separata non sono verificati/approvati. Il path di output riportato in Sessione 43 è assente; non è il parent preesistente richiesto. Non scegliere `/tmp`, home, checkout o altri path per supposizione. Di conseguenza questo schema non va copiato in shell né dichiarato comando approvato. Dopo la riconciliazione, sostituire i tre nomi simbolici con letterali esatti nella copia sottoposta a review; la review deve ricontrollare la command string e i suoi hash.

```bash
# Queste tre variabili devono essere fissate a path assoluti approvati prima
# di autorizzare una copia finale del comando. Questo blocco si ferma se mancanti.
: "${CHECKOUT:?checkout guest assoluto approvato non impostato}"
: "${EVIDENCE_PARENT:?parent collector preesistente approvato non impostato}"
: "${CAPTURE_DIR:?capture directory esistente approvata non impostata}"
if [[ $CHECKOUT != /* || $EVIDENCE_PARENT != /* || $CAPTURE_DIR != /* ]]; then
  printf '%s\n' 'ERROR: tutti i path devono essere assoluti' >&2
  exit 64
fi
set -o pipefail
set -o noclobber
exec 3> "$CAPTURE_DIR/runner.exit-status" || exit 73
{
  /usr/bin/cat -- "$CHECKOUT/qualification/alpha-m1-verified-execution-bootstrap.py" |
    /usr/bin/env -i PATH=/usr/bin:/bin LANG=C LC_ALL=C TZ=UTC \
    /usr/bin/python3 -I -S -c 'import hashlib,sys
expected=sys.argv[1]
source=sys.stdin.buffer.read(262145)
if len(source)>262144 or sys.stdin.buffer.read(1):
 print("ERROR: bootstrap source exceeds size limit",file=sys.stderr);raise SystemExit(70)
actual=hashlib.sha256(source).hexdigest()
if actual != expected:
 print("ERROR: bootstrap SHA-256 mismatch",file=sys.stderr);raise SystemExit(70)
args=sys.argv[1:]
sys.argv=["<verified-bootstrap>",*args]
ns={"__name__":"__main__","__file__":"<verified-bootstrap>","__package__":None,"__alpha_m1_verified_bootstrap_sha256__":actual}
exec(compile(source,"<verified-bootstrap>","exec",dont_inherit=True),ns,ns)
' \
    3a5aa58d0a87b1363e61180773b4fd0318b930d41b81186a39ff0fd40a71f9a6 \
    f67a5dde11add6ced5802fa8091680f75f480a43fcc386f2049844f313b2f469 \
    "$CHECKOUT/qualification/alpha-m1-verified-evidence-runner.py" \
    --execute --output-parent "$EVIDENCE_PARENT" \
    --candidate-dir "$CHECKOUT/qualification"
} > "$CAPTURE_DIR/runner.stdout" 2> "$CAPTURE_DIR/runner.stderr"
rc=$?
printf '%s\n' "$rc" >&3 || exit 74
exec 3>&-
```

`exec 3>... || exit 73` è un controllo fail-closed: se status path esiste o non è apribile, il pipeline non parte. `noclobber` tutela i tre file capture esistenti; la redirezione sul compound command cattura anche diagnostica di `cat`. `pipefail` fa arrivare `cat` o Python failure allo status del pipeline; `rc` viene scritto separatamente. Ogni errore di redirect, status, runner, segnale o timeout rende la raccolta parziale/ambigua: conservare ciò che esiste, non riprovare, non correggere o pulire.

**Per Sessione 44 il comando rimane un template, non soddisfa la richiesta di un comando completo senza placeholder.** Il motivo è il prerequisito mancante dei path guest già esistenti e approvati e della loro relazione checkout/output/capture. Compilarli ora inventerebbe un'operazione e non è autorizzato.

## Precondizioni obbligatorie

- Binding host UUID/snapshot ↔ guest ottenuto secondo [checklist Sessione 44](alpha-m1-session-44-host-guest-reconciliation.md); se il legame non è dimostrato, stop.
- Tutti gli hash sopra ricalcolati nel checkout VM approvato e confrontati byte per byte; verificatore storico PASS 149/digest fissato.
- Revisione/approvazione dell'intero read set Sessions 37/38/40, utente guest non-root, compatibilità runtime, destinazioni, ACL, mount e nomi capture assenti.
- Comando finale con path letterali costruito e revisionato da Core/Security/Operations; approvazione umana firmata per **una sola** acquisizione.

## Divieti, stop e conservazione

Nessun test, CTest, canary, staging, installazione, rollback, cleanup, modifica systemd/cgroup/mount/permessi o operazione snapshot è incluso. Fermarsi per ogni discordanza d'identità/hash, symlink, hard link, file speciale, ownership/mode/ACL/mount inatteso, privilegi richiesti, accesso negato, input inatteso, segnale, timeout, errore, processo non terminato o output parziale. Non uccidere processi, non ritentare e non riparare. Conservare raw stdout/stderr/status/record e directory collector incompleta; qualificare ogni risultato ambiguo BLOCKED e richiedere nuova review.
