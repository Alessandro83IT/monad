# Alpha M1 verified evidence runner

> **Historical guide; superseded by the Session 48 candidate launch review.** The pins and launch guidance below describe the prior Session 42 baseline. Do not use them for the current candidate; preserve this page as historical context and use [Session 48 verified launch](../docs/qualification/alpha-m1-session-48-verified-launch.md) only after its candidate pins are explicitly approved.

The runner's source must be loaded through the authenticated bootstrap procedure in [verified execution architecture](alpha-m1-verified-execution.md). Direct `python3 runner.py` invocation is intentionally rejected. The Session 39/40/41 procedures describe earlier runner identities and are historical; this Session 42 document supersedes their launch syntax but does not retroactively approve this runner.

The runner remains verification-only by default. A real collection still requires separate, explicit human authorization naming the exact VM, snapshot, runner, bootstrap, loader, collector, input scope, and output paths. Session 42 did not execute it.

## Collector handling

The code fixes the collector path and SHA-256 to `6527d34cbfcca75244a77a1457f13e3d8e1b79aee5393f348c73bc96d74a74d2`. It opens that path without following a final symlink, requires a singly-linked regular file, copies the opened bytes into a Linux `memfd`, checks the digest, and applies write, grow, shrink, and seal seals. Before Bash launch it verifies the complete seal set and rehashes the descriptor. Bash reads the sealed descriptor through `/proc/self/fd/N`; it does not reopen the collector pathname.

The runner emits provenance to stderr and collector output to stdout. The operator must capture both with a no-clobber method in separately approved, private, pre-existing locations and preserve the supervising process wait status. The descriptor may be inherited by Bash children. Sealing pins the collector bytes only; it does not authenticate Bash, utilities, kernel, VM, data read by the collector, or output durability. A crash or `SIGKILL` can leave partial output without a final record.

No arbitrary collector path is accepted. `--execute` is only an explicit execution switch, not an authorization mechanism. Do not run as root. No weaker fallback exists when Linux memfd sealing or `/proc` is unavailable.
