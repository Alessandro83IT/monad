# Alpha M1 Session 34 — Core transaction safety report

## Scope and candidate identity

Core changed only `qualification/install-athena-alpha-m1-admin.sh` and this
report. The installer SHA-256 after the final Verification run is:

`157f080c4d5692af9a6f3ea8ad8b778165bd5a60c4a13b84b4cef4f0aea2c770`

The previous trap called `rollback_all` after a nonzero exit with
`install-started`. That implicit restoration path is removed. The EXIT trap
now records a best-effort `install-partial` marker after a post-marker failure
and preserves backups and staged files. Before publication begins, it removes
only private temporary files. INT and TERM use the same exit handling. SIGKILL
cannot run a trap; the durable `install-started` marker and target hashes leave
the transaction identifiable for manual review.

## Transaction and approval model

Install remains `install ROOT_BACKUP_DIR`. The directory basename is the
transaction ID and the path is exclusive: existing paths are refused. Before
creating another transaction, the installer scans the backup root and stops
when it finds a transaction without a terminal install or rollback marker,
including a partial rollback. A persistent `flock` serializes invocations.

The transaction directory contains the existing backup payload and
`backup-manifest.json`, plus canonical sorted compact `transaction.json`
(schema 1, newline terminated). The latter binds the transaction ID, backup
manifest SHA-256, fixed destination paths, each destination's original
presence/hash/uid/gid/mode, and candidate hashes. Before writing it, Core
checks that each live original still matches its captured backup bytes and
metadata.

Markers are exclusive, synced files: `prepared`, `install-started`,
`install-partial`, `install-complete`, `rollback-started`, `rollback-partial`,
and `rollback-complete`. Normal markers identify the transaction; the partial
install marker records transaction ID and exit code. Three destination
replacements remain separate atomic renames, not a single transaction. A
failure after `install-started` stops further publication and leaves any mixed
generation, backup, marker, and temporary evidence for review.

Rollback requires the distinct command:

```sh
install-athena-alpha-m1-admin.sh rollback ROOT_BACKUP_DIR \
  --transaction-id ID --approval-file ABSOLUTE_PATH --approval-sha256 SHA256
```

Before the first restore, the installer validates the explicit transaction ID,
canonical backup manifest and transaction record, exact fixed destination
inventory, approval-file digest, and every current destination against the
recorded original or candidate hash. Approval JSON must bind schema 1, action
`rollback`, transaction ID, backup manifest hash, and transaction record hash.
The file must be a regular mode-0600 file owned by the invoking administrator,
with no ACL, hardlink, or symlink in any path component. `rollback-started` is
exclusive and makes rollback one-shot. Any restore or status error stops later
restores, writes `rollback-partial` best-effort, and blocks a retry. Manual
recovery requires a new review.

The approval JSON is unsigned. Its file and supplied SHA-256 bind the exact
approval bytes to the transaction, but they do not prove who approved it or
protect against a compromised root account. A human administrator must record
the separate approval decision in an independent audit location.

Canary receipt validation now walks every absolute path ancestor using
descriptor-relative `O_NOFOLLOW | O_DIRECTORY` opens, then opens the receipt
relative to the pinned parent descriptor with `O_NOFOLLOW`. The systemd/cgroup
receipt semantics and schema are unchanged.

## Test seam and verification

The installer accepts `MONAD_ALPHA_M1_TEST_ROOT` only for a non-root caller.
It requires an existing caller-owned mode-0700 root and redirects all mutable
paths beneath it. Test mode skips VM-only checks and cannot use production
paths. Named fault points cover pre-marker, post-marker, each rename, and a
partial rollback. A bounded release-file pause supports deterministic
SIGINT/SIGTERM tests; the kill hooks exercise SIGKILL. This mode exercises the
same shell transaction transitions used by administrative mode, without
claiming root ownership or VM behavior.

Verification ran the following on the exact candidate hash above:

- **PASS:** `bash -n qualification/install-athena-alpha-m1-admin.sh`.
- **PASS:** 25 non-privileged static, transaction, and receipt-path tests.
- **FAIL:** 0.
- **SKIP:** 0.
- **TIMEOUT:** 0.
- **NOT RUN:** CTest, installer administrative mode, live canary, VM checks,
  and the 18 privileged cases.

The full cases and evidence scope are in the [Verification report](alpha-m1-session-34-verification-report.md).

## Limits and disposition

Per-file rename atomicity does not provide all-file atomicity. SIGKILL, kernel
failure, and power loss can leave a mixed installation or partial rollback;
markers and hash-bound records identify these states, but they do not restore
them automatically. SIGKILL is covered by deterministic process fault
injection; actual power-loss durability and ext4 recovery were not tested.
Changes by another privileged process outside the installer are not serialized
by its lock. Local root-owned records and unsigned approval JSON cannot resist
compromised root.

The old runner, support, and canary pins were not changed. Security reviewed
this exact installer hash and confirmed the rollback defect is closed, but
returned **BLOCKED for administrative promotion** because the pins still reject
the corrected runner, support, and canary artifacts. Security checked the
pin-only proposal as content hash
`2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7`; it has
not been applied or approved as a final installer. It requires the explicit
pin promotion workflow and a fresh Security review of the resulting installer
hash and manifest.

No administrative action, installation, live canary, or privileged test was
performed. The verified transaction candidate is not ready for administrative
approval until pin reconciliation and the new final-candidate review are
complete.
