# Session 48 — Verified collector runtime compatibility

**Disposition: PATCH VERIFIED; candidate pins only.** The memfd/`$0` path defect is fixed in the candidate source and covered by synthetic tests. This is not an approved pin promotion, VM preflight, acquisition authorization, or Alpha M1 qualification. Session 47 report file was not present in this checkout; its reported finding was used as described in the Session 48 request and reviewer feedback.

## Root cause and path

The runner loads the collector from the checkout with `O_NOFOLLOW`, checks its regular-file/single-link identity and stable metadata, hashes the bytes, copies them into a memfd, applies all four immutability seals, rereads the seals and digest, then launches `/bin/bash /proc/self/fd/N` with the descriptor inherited. Bash therefore sees a proc-fd locator in `$0`, not the checkout path.

The old collector used `$0` with `realpath -e` to infer `SCRIPT_DIR`, then searched there for the CTest helper. It also required `$0` not to be a symlink before recording its own digest. Those assumptions do not fit the memfd launch: the helper path could not reliably refer to the candidate checkout, and the self-hash gate rejected `/proc/self/fd/N`.

## Architecture choices

1. **Use an explicit candidate directory throughout (chosen).** Require `--candidate-dir`; in the verified runner bind it exactly to `<bootstrap-verified-checkout>/qualification`; open every directory component with `O_DIRECTORY | O_NOFOLLOW`; pass that same absolute path to the collector. The collector checks that it is absolute, existing, and canonical, and uses it to resolve the pinned helper. The helper is opened no-follow, required to be regular/singly linked/bounded/stable, hashed, and compiled from the same captured bytes. The collector handles `$0` only as a source locator for its own memfd digest, never as a checkout locator.

   This preserves the existing loader/bootstrap/runner/sealed-collector chain and changes only the collector execution interface. The remaining pathname check/use race for candidate data is same-UID pathname risk; content executed as helper remains pinned and executed from the bytes whose digest passed.

2. **Make the memfd appear to have a checkout pathname.** A Bash `-c` wrapper could set `$0` to the checkout script path while consuming the memfd. This was rejected: `$0` would describe a mutable pathname rather than the source used to start Bash, and the self-hash could accidentally hash different bytes from the sealed payload.

3. **Snapshot and pass a second helper memfd.** This would further reduce helper path races but adds descriptor lifecycle and another execution channel without improving the current pinned helper's same-buffer hash/compile guarantee enough to justify the complexity. It was not selected.

No privileged operation or diagnostic read-set expansion was introduced. The candidate directory had already been a collector input for the Session 34B files and manifest; this change points the helper lookup to that same approved directory. The collector still does not read D6 payloads.

All launch commands and pin values published before Session 48—including Sessions 35 and 37–44, the earlier verified-runner guide, and prior authorization/preflight templates—describe historical interfaces and baselines. In particular, any earlier statement that `--candidate-dir` may be omitted is superseded by this candidate interface. Preserve those records as historical evidence; do not use their launch commands or digests for this candidate.

## Changes and verification

- Runner `--execute` now requires explicit `--candidate-dir`, verifies it before opening the collector, binds it to the bootstrap-injected checkout root, and rechecks the component path before launch.
- Collector no longer defines or uses `SCRIPT_DIR`; candidate-dir is mandatory, validated before output creation, and is the sole helper base. `$0` is accepted for collector hashing only as the expected `/proc/self/fd/N` locator or as a regular non-symlink script file. The report identifies memfd execution.
- Pinned helper opening now rejects hard links and compares link count/mode as part of metadata stability.
- Bootstrap and inline loader are unchanged. Session 44 reports and historical pin values remain untouched.
- Synthetic coverage exercises Bash through a sealed memfd, `$0` proc-fd behavior, hashing the inherited fd, candidate paths containing spaces, missing/symlink/noncanonical candidate paths, candidate mismatch, helper path resolution, missing/symlink/hardlink/wrong-digest helpers, in-place helper mutation, digest-record write failure, argument rejection, nonzero exit, signal handling, output-record failure, and read-set boundaries.

Command executed:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v \
  tests.alpha_m1_verified_evidence_runner_test \
  tests.alpha_m1_vm_evidence_collector_test \
  tests.alpha_m1_verified_execution_bootstrap_test
```

Result: **72 PASS, 0 FAIL, 0 SKIP, 0 TIMEOUT**. These tests use synthetic payloads, temporary directories, and extracted collector functions. They do not execute the real diagnostic collector, run CTest, or inspect D6 payloads. The suite did not run as CTest.

## Candidate pin set

These digests identify the current candidate bytes. The prior Session 44 pins remain historical and are not replaced or approved by this report.

| Artifact | Prior Session 44 pin | Session 48 candidate SHA-256 | Change / coverage |
|---|---|---|---|
| Inline loader (test constant and documented launch source) | `feef002511128263daf5fa05fcaa72686a688d58fb717b2f8d1a9d026b205021` | `feef002511128263daf5fa05fcaa72686a688d58fb717b2f8d1a9d026b205021` | Unchanged; bootstrap tests verify exact constant. |
| Bootstrap | `3a5aa58d0a87b1363e61180773b4fd0318b930d41b81186a39ff0fd40a71f9a6` | `3a5aa58d0a87b1363e61180773b4fd0318b930d41b81186a39ff0fd40a71f9a6` | Unchanged; bootstrap test module PASS. |
| Verified runner | `f67a5dde11add6ced5802fa8091680f75f480a43fcc386f2049844f313b2f469` | `59baa335d0fdb2a313f2f9c0bf257253025d771f662d37807d97402f8fcdfe30` | Candidate path validation, required interface, new collector pin; runner tests PASS. |
| Collector | `6527d34cbfcca75244a77a1457f13e3d8e1b79aee5393f348c73bc96d74a74d2` | `ec222eae049c8469217f15c93043e82bf4bc7308d86499044aebc902550d760e` | Explicit candidate/helper path and memfd self-hash handling; collector tests PASS. |
| CTest inventory helper | `1d95eb5c781f4f21581a01a2b778e097535221af3b1a9b7610ad81cf576c87d1` | `1d95eb5c781f4f21581a01a2b778e097535221af3b1a9b7610ad81cf576c87d1` | Unchanged; helper pin/mutation tests PASS. |

The runner's embedded collector digest now names the Session 48 candidate collector. The previous approved digest remains in Session 44 records; no approval record or historical evidence was edited. A human pin-promotion review must approve the complete candidate set before a guest preflight can use it.

## Command — NON ESEGUIRE

This literal uses the checkout and output path values previously supplied for planning. Their existence, identity, ACLs, mounts, ownership, capture-name absence, and guest binding have not been verified in this development session. The command is not authorization and must not be run until the candidate pin set is approved and a new same-window guest/host preflight and one-shot human approval are complete.

```bash
set -o pipefail
set -o noclobber
exec 3> /home/alessandro/monad-alpha-m1-capture/runner.exit-status || exit 73
{
  /usr/bin/cat -- /home/alessandro/Projects/monad/qualification/alpha-m1-verified-execution-bootstrap.py |
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
    59baa335d0fdb2a313f2f9c0bf257253025d771f662d37807d97402f8fcdfe30 \
    /home/alessandro/Projects/monad/qualification/alpha-m1-verified-evidence-runner.py \
    --execute --output-parent /home/alessandro/monad-alpha-m1-collector-parent \
    --candidate-dir /home/alessandro/Projects/monad/qualification
} > /home/alessandro/monad-alpha-m1-capture/runner.stdout \
  2> /home/alessandro/monad-alpha-m1-capture/runner.stderr
rc=$?
printf '%s\n' "$rc" >&3 || exit 74
exec 3>&-
```

Any mismatch, path issue, denied read, output collision, interruption, timeout, nonzero exit, or ambiguous record means stop and preserve all partial files without retry, repair, or cleanup. No privileged tests, canary, installation, rollback, D6 cleanup, VM operation, commit, or push is authorized.
