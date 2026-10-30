# Session 42 Verification report

**Disposition: PASS.** The final command was:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v \
  tests.alpha_m1_evidence_tree_digest_test \
  tests.alpha_m1_verified_evidence_runner_test \
  tests.alpha_m1_verified_execution_bootstrap_test
```

Result: **42 PASS, 0 FAIL, 0 SKIP, 0 TIMEOUT** (6 evidence-tree tests, 15 runner tests, 21 bootstrap tests).

The inline loader expected hash, its test constant, and the documented command source all match byte-for-byte at `feef002511128263daf5fa05fcaa72686a688d58fb717b2f8d1a9d026b205021`. Bootstrap tests exercise the production bootstrap source with synthetic temporary runner payloads. Runner tests use temporary fixtures or synthetic memfds. The verification-only runner test passes the repository collector path to a mocked `snapshot_collector`; it does not open or inspect that path. Direct unverified runner invocation exits before collector resolution. No real collector was opened or executed.

Coverage includes tree framing and path ordering, count/content changes, symlink/special files, root restrictions, directory/file mutation; runner digest, open/read errors, symlink/hardlink, memfd seals and rehash, path replacement, concurrent mutation, signal forwarding, output/status, environment isolation, and launch gate; bootstrap digest/size/syntax, path traversal, symlink/special files, descriptor closure, EINTR, replacement before/after verification, concurrent mutation, import isolation, signal, and end-to-end synthetic execution.

Limits: the unreadable-file case may skip under root-like permissions, though it did not skip in this run. This test command does not inspect the historical 149-file corpus; the separate read-only tree verifier did so and reported 149 files / expected digest PASS. Neither the suite nor local hashes authenticate the VM, Python/runtime, kernel, or approval source.
