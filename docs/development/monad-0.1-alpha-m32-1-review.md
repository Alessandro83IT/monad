# Monad 0.1 Alpha M1 — M32.1 read-only acquisition review

## Verdict

**GO_FOR_OPERATOR_REVIEW.** This is a review candidate only. It does not authorize privileged execution, production acquisition, or Alpha M1 qualification. No production acquisition or privileged operation was run in this session.

## Root causes

M32's traversal counter bounded entries passed to `walk`, but `snapshot()` only incremented `enum_scanned`; it never compared that count with a limit. Each directory is enumerated once to obtain traversal names and again to verify the directory after visiting its children. The first pass can consume one extra look-ahead name to recognize truncation, and the verification pass can independently enumerate the whole directory plus its look-ahead. A large single directory, or many directories whose verification scans are repeated, therefore had no global enumeration work bound. The traversal-entry limit did not bound those directory reads.

M32 synchronized the output files and output directory. It did not synchronize the output parent after `mkdir`, so the new destination directory entry could be lost after a crash even if the contents had been synchronized.

## M32.1 implementation

Added [m32-1-readonly-acquisition.sh](../../qualification/m32-1-readonly-acquisition.sh), [m32_1_readonly_acquisition_test.py](../../tests/m32_1_readonly_acquisition_test.py), and this review. Existing M32 and earlier artifacts remain untouched. M32.1 is based on M32's descriptor-relative traversal, identity checks, mount/device boundary checks, metadata-only policy, exact hash allowlist, private output modes, checksums, and preservation of evidence on failure.

The collector now has an independent `enumeration` limit (default 20,000 yielded directory entries; hard maximum 200,000) in addition to the existing `entries` traversal limit (default 10,000; hard maximum 100,000). The inventory reports `enumeration_entries_consumed`.

For output durability, the collector calls and checks `fsync` on the anchored parent immediately after creating the destination directory. It then synchronizes each output file, the output directory after file creation, and the anchored parent again after the files have been created. It continues to write through the descriptors opened relative to the validated parent. The supplied parent and destination names are re-resolved and compared with the anchored descriptors before success; replacement fails closed. Existing root ownership, writable-mode, output-directory mode `0700`, and file mode `0600` checks are retained.

## Enumeration accounting invariants

- `entries` counts each node admitted to `walk`, including scoped roots and descendants. It is charged before stat/open work. No node beyond the limit is visited; a required node that cannot be visited records `LIMIT_ENTRIES` and makes the inventory `PARTIAL`.
- `enumeration` counts each `DirEntry` yielded to the collector by `scandir`. It is global across all scoped paths, all directories, initial snapshots, verification snapshots, and look-ahead names.
- A yielded name used only to establish overflow is charged. No name yielded by a verification pass is free.
- The collector checks the global counter before asking the iterator for another name. Once the budget reaches zero it does not call `next()` again, because that could consume an additional entry. Since it cannot know whether an uninspected iterator is already at EOF without that call, reaching the budget before establishing iterator exhaustion is conservatively marked incomplete.
- A local traversal cap may also cause one yielded look-ahead entry to be charged. The directory is marked `LIMIT_ENTRIES`; the inventory cannot be `COMPLETE`.
- Failure to finish either the initial required enumeration or the subsequent verification records an incomplete verification status and makes the inventory `PARTIAL`. Changed-directory conclusions are made only when both required snapshots completed. A missing observation is never represented as a stable directory.
- The budget bounds names yielded/consumed by Python iteration. The kernel/filesystem may internally read or buffer directory blocks as part of an individual `getdents` operation; this userspace policy cannot bound that implementation detail, elapsed I/O latency, or pathological filesystem behavior.

Exact traversal-budget use may complete if all required nodes fit and no further node is needed. Exact enumeration-budget exhaustion is intentionally conservative when another iterator step would be required to establish completion. The reported consumed count never exceeds its configured budget.

## Durability guarantees and limits

Successful completion requires successful `fsync` of both output files, the output directory, and the anchored parent directory. The first parent sync persists the new destination name; the later sync persists names created for evidence files. Any reported fsync error prevents successful completion and leaves already-created output in place for review. The candidate does not delete, rename away, or silently replace partial evidence.

These calls request persistence from the filesystem and storage stack. They cannot promise behavior from filesystems, virtualized storage, controllers, or devices that do not honor flushes; they do not provide atomic multi-file transactions, remote replication, independent provenance, or protection against later media failure. A failure before inventory writing can leave an empty destination directory. A failure during writes/fsync can leave partial files. Both are retained and the command returns failure.

Output directory and parent replacement tests confirm that writes remain anchored to the opened descriptors and that a changed pathname is never reported as successful completion. Parent ownership and mode policy is unchanged.

## Adversarial and regression verification

All runs were unprivileged and used temporary synthetic fixtures. No network, sudo, production identity path, installer, runner, rollback, canary, mount, systemd, or VM snapshot operation was used.

| Check | Result |
|---|---|
| M32.1 suite: `python3 tests/m32_1_readonly_acquisition_test.py` | 49 tests: 48 PASS, 1 SKIP, 0 FAIL, 0 TIMEOUT |
| M32 regression: `python3 tests/m32_readonly_acquisition_test.py` | 45 tests: 44 PASS, 1 SKIP, 0 FAIL, 0 TIMEOUT |
| M31 regression: `python3 tests/m31_readonly_acquisition_test.py` | 41 tests: 40 PASS, 1 SKIP, 0 FAIL, 0 TIMEOUT |
| `bash -n qualification/m32-1-readonly-acquisition.sh` | PASS |
| Embedded Python compilation | PASS |
| M32.1 Python test compilation | PASS |
| Synthetic JSON schema keys, SHA256SUMS digest, directory/file modes | PASS |
| `git diff --check` for M32.1 artifacts | PASS |

The sole skip in each test suite is synthetic Unix socket creation, denied by the environment with `EPERM`. No test timed out. The M32.1 adversarial cases cover exact/insufficient/over traversal budgets, exact and insufficient enumeration budgets, look-ahead, repeated verification, wide and deep trees, incomplete-verification reporting, parent fsync success/failure, output directory and parent replacement, preservation after write/file/directory fsync failures, output modes, checksums, and inherited M32 identity, traversal, mount boundary, hashing, special-file, and output security invariants.

## Remaining risks

- Enumeration is bounded by entries yielded to userspace, not by kernel directory-block prefetch or storage latency.
- Metadata traversal is non-atomic. A transient change that occurs and is reversed between observations may not be detected.
- `fsync` durability depends on the filesystem and storage stack honoring synchronization requests. No power-loss or actual storage-failure campaign was performed.
- The approved Debian VM, real mount topology, production filesystem behavior, and administrative output environment were not inspected. There is no privileged qualification evidence and no Alpha M1 qualification claim.
- The tests use synthetic fault injection for fsync failures and pathname replacement; they do not substitute for operator review or the separately controlled VM campaign.

## Final disposition

**GO_FOR_OPERATOR_REVIEW.** The M32.1 source and synthetic evidence are ready for independent review. This verdict grants no authorization for privileged execution or production acquisition and does not qualify Alpha M1.
