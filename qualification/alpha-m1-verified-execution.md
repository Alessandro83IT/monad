# Alpha M1 verified Python runner execution

## Trust chain

The executable Python code is loaded in two authenticated stages. An operator reviews and approves the exact inline loader below (SHA-256 `feef002511128263daf5fa05fcaa72686a688d58fb717b2f8d1a9d026b205021`), then supplies the full approved bootstrap and runner SHA-256 values. The loader reads bounded bootstrap bytes from stdin, hashes those bytes, and compiles that same `bytes` object. The bootstrap walks an absolute canonical path from `/` using directory descriptors and `O_NOFOLLOW`, opens the fixed runner basename without following symlinks, requires a singly linked regular file, and reads through that descriptor. It hashes the captured source, checks metadata stability and length, closes descriptors, then re-hashes the immutable `bytes` object and compiles that same object. It never reopens the runner path for code loading. Replacing the path after its open cannot change the captured source; a replacement before open must still match the independently approved runner pin.

This establishes byte identity conditional on the approved inline loader and supplied pins. It does not authenticate the shell, `cat`, `env`, Python executable, dynamic loader, standard library, kernel, repository checkout, VM, or approval process. Those are part of the external trust boundary and must be checked by the operator. Python runs with `-I -S` and an empty controlled environment, so user site packages, `PYTHONPATH`, `PYTHONHOME`, current-directory imports, and site initialization are excluded. The runner uses standard-library imports only. Python itself and its standard library remain trusted.

The runner source is compiled directly from immutable in-memory bytes rather than a memfd. Its code object is therefore produced from exactly the byte object whose hash was checked. The collector is a separate payload: the runner opens the fixed collector, copies and hashes it into a memfd, applies and verifies `F_SEAL_WRITE`, `F_SEAL_GROW`, `F_SEAL_SHRINK`, and `F_SEAL_SEAL`, then rechecks the digest before passing the descriptor to `/bin/bash`. This does not pin the Bash interpreter, its child utilities, collector inputs, or output files. The memfd is intentionally inherited by Bash for `/proc/self/fd/N` access.

## Reviewed manual launch form

The following is a **verification-only form**, not authorization to run against a VM and not an acquisition command. The exact inline loader is pinned above and tested against the literal in `tests/alpha_m1_verified_execution_bootstrap_test.py`. Before any future use, independently review the checkout and all three identities (inline loader, bootstrap, runner); copy their full hashes into the human approval record. Never accept abbreviated hashes. Use absolute paths and a reviewed interpreter path. Example variables below are placeholders that must be replaced by literal approved values; this block was not run in Session 42.

```sh
set -o pipefail
/usr/bin/cat -- /reviewed/checkout/qualification/alpha-m1-verified-execution-bootstrap.py |
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
  /reviewed/checkout/qualification/alpha-m1-verified-evidence-runner.py
```

The loader source in the shell command, including its final newline, is byte-for-byte identical to the pinned `INLINE_LOADER` test constant. A regression test checks the constant against the published digest; reviewers checked the command text against that constant. The command has no runner arguments, so it asks only for collector verification. `pipefail` preserves a `cat` failure. This recipe must be independently reviewed and authorized before use. A future `--execute` invocation requires a distinct approval naming the exact VM and snapshot, full inline-loader/bootstrap/runner/collector hashes, input scope, output capture paths, allowed actions, prohibited actions, and stop conditions. The runner's `--execute` flag is not authorization.

## Failure and interruption behavior

Digest mismatch, unexpected path shape, symlink, special file, hard link, unstable metadata, read error, syntax error, or bootstrap identity mismatch fails closed before runner code executes. A runner source replacement after successful capture does not change the in-memory bytes. `SIGINT` before the runner starts returns the bootstrap interruption status; normal `SIGTERM` terminates the outer interpreter before code runs. Once runner code begins, its existing signal forwarding applies to its child process. `SIGKILL`, power loss, or interpreter failure may prevent diagnostics from being written. The runner has no internal wall-clock timeout; an approved external supervisor may impose one, and any timeout/partial output must be retained as incomplete evidence without automatic retry.

The collector source memfd requires Linux memfd sealing and `/proc/self/fd`; there is no weaker fallback. The Python source bootstrap uses Linux `openat`-style descriptor-relative opens and `O_NOFOLLOW`. Debian 13's system Python 3 and Linux kernel are expected to provide these interfaces; this expectation is not a substitute for VM-side version and behavior preflight. No privileged operation is required.

## Candidate directory and memfd execution

For `--execute`, the runner requires an explicit `--candidate-dir`. When loaded by the verified bootstrap, it must be exactly the authenticated runner checkout's `qualification` directory. The runner walks every path component with `O_DIRECTORY | O_NOFOLLOW`; it rejects relative, noncanonical, missing, symlinked, or mismatched paths before reading the collector. It forwards the same absolute candidate path to the collector.

The collector never derives the checkout or helper location from `$0`. In the verified launch, Bash receives the script as `/proc/self/fd/N`, and `$0` is only a locator for the sealed anonymous collector bytes. The collector uses the explicit canonical candidate directory for checkout data and resolves `alpha-m1-ctest-inventory.py` beneath it. The helper is opened without following the final symlink, must be regular, singly linked, bounded, and stable while read, and is hashed against its fixed helper pin. The exact captured byte buffer whose digest passed is compiled; its pathname is not reopened to execute code. A helper read/digest/write failure remains BLOCKED.

The collector's own SHA evidence follows the exact `/proc/self/fd/N` locator only when `$0` has that expected form and is readable; the runner passes the descriptor to Bash. The report labels this as sealed-memfd execution, not as a filesystem source path. Ordinary script-file invocation can still hash its regular non-symlink `$0`, but the collector requires `--candidate-dir` in all modes and has no cwd or script-path fallback.

This interface change requires a new collector and runner candidate baseline. The Session 44 pins remain historical and are not authorization for these bytes. See the Session 48 candidate launch review before any future operator approval.
