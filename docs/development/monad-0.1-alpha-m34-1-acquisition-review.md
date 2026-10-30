# Monad 0.1 Alpha M34.1 — targeted forensic acquisition review

**Scope verdict: GO_FOR_OPERATOR_REVIEW. Privileged acquisition: NO-GO pending explicit authorization. Alpha M1: NO-GO / NOT QUALIFIED.** This review covers the local collector and synthetic tests only. No VM, privileged path, M32.1 bundle, backup, installer, runner, sudoers file, or administrative state was accessed in this session.

## 1. Purpose

M34.1 supplies a narrowly scoped, read-only collector for a future authorized Debian Alpha M1 guest session. It is intended to preserve enough of the installed runner/support/sudoers generation, the named installer backup transaction, and the existing M32.1 bundle to support independent reconstruction. It does not identify the cause of the reported discrepancy, validate an installer transaction protocol, or authorize reconciliation.

## 2. Threat model

The collector treats inspected paths as untrusted and potentially changing. It assumes the invoking Python 3 interpreter and the collector bytes themselves are trusted for that invocation; neither has an external signature or attestation in this repository. It defends against symlink traversal, special files, path substitution, hard links, writable administrative objects, excessive traversal and oversized content. It does not defend against a malicious kernel, a compromised interpreter, a privileged concurrent actor capable of restoring metadata, or a dishonest acquisition operator.

## 3. Acquisition perimeter

The only source roots are the following fixed paths. There is no filesystem-wide search, glob, `find`, package query, network access, subprocess execution, or administrative command invocation.

| Root | Role |
|---|---|
| `/usr/local/sbin` | Metadata inventory; exact runner and support bytes/hash |
| `/etc/sudoers.d` | Metadata inventory; exact `athena-alpha-m1` bytes/hash |
| `/root/athena-alpha-m1-stage` | Metadata inventory; exact staged `gateway_support.py` bytes/hash |
| `/var/lib/athena-alpha-m1` | State and backups metadata; hashes of regular files under the named transaction; exact transaction records and known markers copied. The `ext4-fixture` directory itself is recorded, but its children are pruned. |
| `/var/lib/monad-m32-1-evidence-20261029T194120Z` | Metadata inventory; exact `inventory.json` and `SHA256SUMS` copy and source checksum check |

The only required leaves are the installed runner, installed support, installed sudoers file, `/var/lib/athena-alpha-m1/backups/run-20261021-061921`, and the M32.1 `inventory.json` plus `SHA256SUMS`. Missing required evidence is recorded as `REQUIRED_ABSENT` and prevents `COMPLETE`. Stage files are conditional observations. The directory roots above deliberately constrain the walk; unrelated system locations, logs, D6 fixture contents, `/root` generally, and other backup runs are outside scope.

## 4. Path and content contract

Every source root and descendant is opened relative to an already-open directory descriptor using `O_NOFOLLOW`; leaf symlinks are recorded but never opened. Only regular files and directories are opened. Special files are metadata-recorded and not opened. Device changes stop descent. Regular files with multiple hard links, group/world writable objects, owner mismatches, or insufficient read/search bits are anomalies. UID is checked (root in production, current user in synthetic mode); GID is recorded but not pinned.

Raw copies are restricted to exact installed target paths, the exact staged support path, M32.1 inventory/checksum, and known transaction records/markers in the named backup directory. Other files in that one transaction directory are SHA-256 hashed but not copied. Other files in the fixed roots receive metadata only. No source file is changed. The M32.1 source checksum is checked against the copied inventory; this proves internal consistency only, not that either file is authentic.

The output argument must be absolute, normalized, outside the repository and every source root, and new. Its parent path is opened component by component without following symlinks. The final parent must be private and appropriately owned; sticky `/tmp` ancestors are accepted for synthetic tests. The new output directory is mode `0700`; files are mode `0600`. Existing destinations are refused. The output directory and anchored parent are checked again before success. This is not a claim of whole-tree atomicity or durable-media guarantees.

## 5. VM identity

Production mode requires Linux and pins hostname `debian`, the machine ID, product UUID, product serial, and Oracle/Innotek vendor identifiers to the Alpha M1 guest values recorded by the prior M32 contract. It reads fixed identity files through a no-follow component walk and records the boot ID and kernel. This is a guest-side identity check, not independent host virtualization evidence: it does not verify the host VM UUID, current snapshot, snapshot inventory, OS release file/version, or external operator identity. A mismatch or unreadable identity blocks before source collection. Synthetic mode can only be activated with a private caller-owned synthetic root and is refused when effective UID is root; test controls are forbidden in production mode.

## 6. Resource bounds

Hard-coded limits are depth 12, 4,000 walked entries, 8,000 enumerated names, 4 MiB read/hash per file, 32 MiB total source bytes read, and 8 MiB for `inventory.json`. Exceeding a traversal/content bound creates an issue and yields a partial acquisition; no limit is silently treated as completeness. The inventory records counters and limits. Per-file and total limits bound file content reads; traversal output can still be partial when the inventory itself reaches its bound.

## 7. Inventory and classifications

`inventory.json` schema 1 records collector/schema identity, UTC start/end, guest identity observations, exact roots and content policy, resource limits, entry/enumeration/read counters, per-path status and before/after metadata, any SHA-256, copied evidence filename, and issue records. Metadata includes type, mode, UID/GID, device/inode, link count, size, mtime and ctime. Each output file is mode `0600`; `SHA256SUMS` covers the inventory and every copied object.

- `COMPLETE`: all required leaves are present, bounded traversal had no anomaly, and the M32.1 inventory checksum is internally consistent. It does not mean the full M34 evidence plan is satisfied.
- `PARTIAL`: collection finished but one or more required paths, reads, checks, or limits produced an issue.
- `BLOCKED`: preflight refusal or controlled interruption. A refusal before output creation may leave no inventory; an interruption after output creation is represented in a bounded inventory where possible. Preserve any created output.

## 8. Integrity and provenance

SHA-256 identifies the bytes read. Output `SHA256SUMS` detects later changes to output files; the M32.1 checksum is also checked internally. Neither mechanism authenticates the collector, guest, operator, source, timestamp, or transfer chain. There are no signatures, external ledger, trusted clock, or host-side snapshot attestation. A live walk is non-atomic; directory and file identity/metadata are checked around acquisition to detect common concurrent changes, but this cannot prove a consistent point-in-time snapshot.

## 9. M33/M34 findings carried forward

The reported installed runner digest (`5d6ee…21d5`), current installer runner pin (`c68b…9def`), support source digest (`92c9…d514`), missing installed support, and M32.1 `PARTIAL` status remain unverified references. M33/M34 correctly leave the cause and historical generation unknown. M32.1’s metadata-first approach and bounded traversal are useful, but its stated zero hashed bytes and limited backup-name search cannot reconstruct the transaction. M34.1 adds the specifically named backup tree and copies its transaction records while hashing bounded payloads.

## 10. Test results

The dedicated suite uses only temporary synthetic roots and output directories. Results from the final run:

| Check | Result |
|---|---|
| Python suite | PASS — 22 tests (synthetic valid/missing, FIFO, leaf and intermediate symlinks, file/directory substitution, post-open name replacement, file/directory mutation, file/depth/entry bounds, permission and hardlink anomalies, output collision/unsafe parent/symlink parent, interrupted collection, required-leaf classification, M32 checksum mismatch, output checksum and mode checks) |
| Bash syntax (`bash -n`) | PASS |
| Embedded Python compilation | PASS — part of dedicated suite using `compile()`; no generated bytecode artifact |
| SHA256SUMS integrity | PASS — verified by suite against generated inventory and copied content |
| Static scope review | PASS — fixed roots and name allowlist; no subprocess, network, `sudo`, or administrative command execution in collector |
| ShellCheck | SKIP — executable not installed in the environment |
| Privileged/VM tests | NOT RUN — explicitly outside this session’s authorization |
| Sandbox SKIP | None for local synthetic checks |

During development, tests exposed and corrected an unanchored synthetic rename in the race injector, duplicate/overlapping acquisition roots, missing association between copied objects and inventory entries, output-parent symlink following, and inadequate source checksum validation. No historical collector was modified.

## 11. Residual limitations and review findings

- No ACL or xattr capture, mount-ID comparison, filesystem snapshot, boot/snapshot host attestation, trusted time source, raw M32 stdout/stderr/exit status, or approved time-bounded journal/audit/configuration-management logs.
- Hashing/copying the named transaction data is bounded; files over 4 MiB or a tree exceeding entry/depth/byte limits yield `PARTIAL`. There is no attempt to extend limits automatically.
- Filesystem access permissions may prevent reading `/root`, `/etc`, `/usr/local`, or `/var/lib`; the collector records errors and does not escalate. An ordinary user may therefore produce a partial result.
- Source checksum validation checks the expected single `inventory.json` entry in the M32.1 checksum file. It does not establish the authenticity or completeness of the original bundle.
- Metadata and checksums can be modified by a sufficiently privileged concurrent actor; descriptor-relative checks detect common replacement/mutation but do not make a live acquisition atomic.
- `COMPLETE` is local collector completeness against its implemented contract, not completion of M34’s wider forensic plan and not qualification evidence.

## 12. Corrected defects

1. Overlapping source roots could produce duplicate traversal; removed the redundant nested root.
2. A race test initially failed to anchor the destination side of a rename; corrected it and added file and directory substitution tests.
3. Copied content initially lacked a manifest link; each record now names its `evidence_file` and SHA-256.
4. M32.1 source checksums were initially copied but not validated; the inventory digest is now checked, and mismatch is `PARTIAL`.
5. Output parents were initially opened by pathname; they are now traversed descriptor-relatively without following symlinks and revalidated before success. Source names are also rechecked against the opened inode after traversal/read to detect unlink-and-replace races.
6. Backup payloads with nonstandard names were metadata-only; they are now hashed within the exact named backup subtree and not copied.

## 13. Future operator procedure

This is guidance for a separately authorized future session, not an authorization. A reviewer should verify the exact collector bytes and SHA-256, confirm the guest and independent host/snapshot identity, obtain explicit approval naming the account, VM, permitted reads, output parent, and custodian, then invoke the exact reviewed bytes with one new private absolute output path. Do not use `sudo` or retry inaccessible reads with elevated privileges. Preserve any output even on nonzero exit, transfer it through the approved custody channel, and independently verify `SHA256SUMS` and the inventory before interpretation.

## 14. Stop criteria

Stop and preserve output on identity mismatch; existing or unsafe output; source symlink, hardlink, unexpected type/owner/permissions; permission denial; mount/device boundary; file/directory change; M32 checksum mismatch; incomplete traversal; resource limit; interrupted run; output write/fsync error; or any need for privilege escalation. Do not repair markers, change limits to force success, retry as root, or run installer/runner/canary/rollback. A stop is not permission to clean up output.

## 15. Review verdict

**GO_FOR_OPERATOR_REVIEW:** local implementation and synthetic checks pass; independent review of this exact collector and its operating assumptions is appropriate.

**GO_FOR_PRIVILEGED_ACQUISITION:** NO. Requires later explicit authorization and host-side identity/evidence plan; this session does not grant it.

**ALPHA_M1_QUALIFIED:** NO. Alpha M1 remains **NO-GO / NOT QUALIFIED**. No privileged VM campaign or evidence review was performed.
