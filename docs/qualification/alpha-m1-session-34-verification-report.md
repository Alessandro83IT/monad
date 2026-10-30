# Alpha M1 Session 34 — transaction verification

## Scope and result

Verification exercised the installer's isolated rootless test mode under fresh,
caller-owned mode-0700 temporary roots. It did not invoke sudo, VM paths,
systemd, the canary, or privileged tests. The harness uses the transaction
state machine in the candidate installer and injects failures at its named
boundaries; it does not emulate administrative ownership or VM behavior.

Candidate installer SHA-256 observed for the final run:

`157f080c4d5692af9a6f3ea8ad8b778165bd5a60c4a13b84b4cef4f0aea2c770`

Final result: **PASS 25, FAIL 0, SKIP 0, TIMEOUT 0**. No test process timed
out. The installer shell syntax check (`bash -n`) also passed.

## Reproducible command

From the repository root:

```sh
bash -n qualification/install-athena-alpha-m1-admin.sh
python3 -m unittest -v \
  tests.alpha_m1_admin_static_test \
  tests.alpha_m1_installer_transaction_test \
  tests.alpha_m1_receipt_path_test
```

Observed output: `Ran 25 tests in 8.563s — OK`.

## Covered behavior

- Successful installation records a canonical transaction and completion
  marker; repeated use of the transaction path is refused.
- Pre-marker failure leaves targets unchanged. Post-marker failure leaves the
  start and partial markers plus the backup. A new install is refused while
  the prior transaction is incomplete.
- SIGINT and SIGTERM after `install-started` both produce partial state and no
  completion marker. Controlled SIGKILL after that marker leaves the start
  marker without a false partial or completion marker; a new install is then
  refused.
- Injected failure after each of the three successive target renames leaves a
  recoverable partial state with the expected number of candidate files
  published.
- A concurrent invocation is rejected while the first process holds the
  persistent lock.
- Rollback without valid approval, with an identity/hash mismatch, a symlinked
  or hard-linked approval file, altered transaction arrays, or a target whose
  hash is neither the recorded original nor candidate is rejected before
  `rollback-started`; the backup and receipt evidence remain unchanged.
- Explicitly approved rollback restores and verifies the recorded originals.
  Repeating a completed rollback is refused. Injected failure after the first
  restored target records a partial rollback and prevents blind retry.
- Receipt path tests verify a normal absolute directory, reject a symlink in
  an ancestor, reject relative/dot paths, and show that a directory FD remains
  bound to the opened inode after the path is replaced.

These tests use deterministic process-level fault hooks and isolated files.
They do not simulate an actual host power cut, kernel crash, filesystem
corruption, or root-owned VM permissions. SIGKILL is the tested abrupt-process
loss case; power-loss durability still requires review of the fsync ordering
and later verification on the target filesystem.

## Outcome categories

- **PASS:** 25 non-privileged static, transaction, and receipt-path tests;
  `bash -n`.
- **FAIL:** 0.
- **SKIP:** 0.
- **TIMEOUT:** 0.
- **NOT RUN:** CTest; all 18 privileged cases; live canary; installer
  administrative mode; VM receipt, mount, ownership, ACL, systemd/cgroup, or
  power-loss verification.

This report records test results only. It does not establish VM readiness,
administrative approval, privileged qualification, or Alpha M1 qualification.
