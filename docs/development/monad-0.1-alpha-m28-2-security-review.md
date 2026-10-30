# Monad 0.1 Alpha M1 — M28.2 Forensic Acquisition Security Review

## Scope and outcome

M28.2 replaces M28.1's broad regular-file hashing behavior with bounded, metadata-first inventory collection. It remains an explicit, read-only acquisition tool. This Codex session did not run it against protected host paths, did not use `sudo`, and did not perform privileged qualification.

The implementation and synthetic suite are complete for this change. Synthetic checks passed, with the Unix socket fixture skipped because this sandbox rejects `bind(2)` with `EPERM`. This is not a production security approval, privileged acquisition authorization, or Alpha M1 release qualification. **Recommendation: NO-GO for privileged acquisition pending explicit independent review and VM preflight.**

## Files created

- `qualification/m28-2-readonly-acquisition.sh` — M28.2 collector.
- `tests/m28_2_readonly_acquisition_test.py` — synthetic-only regression and fault-injection suite.
- `docs/development/monad-0.1-alpha-m28-2-security-review.md` — this review.

No pre-existing files were overwritten. Existing M28/M28.1 evidence and the dirty worktree were preserved.

## Administrative acquisition allowlist

The collector inventories only these fixed paths, in this order:

1. `/usr/local/sbin/athena-alpha-m1-runner`
2. `/usr/local/sbin/gateway_support.py`
3. `/etc/sudoers.d/athena-alpha-m1`
4. `/var/lib/athena-alpha-m1/admin-install.lock`
5. `/var/lib/athena-alpha-m1`
6. `/var/lib/athena-alpha-m1/backups`
7. `/root/athena-alpha-m1-stage`
8. `/run/athena-alpha-m1`
9. `/var/log/athena-alpha-m1`
10. `/var/lib/athena-alpha-m1/systemd-canary.json`
11. `/var/lib/athena-alpha-m1/ext4-fixture`
12. `/var/tmp/athena-d67-ext4`
13. `/opt/athena-qualification/build`

No caller-provided path is added to this inventory. Traversal is limited to descendants of those fixed roots. The collector does not execute, install, modify, or delete any collected object.

## Hashing policy

The only production content-hash allowlist entry is the exact path `/var/lib/athena-alpha-m1/admin-install.lock`. The administrative installer creates this lock file exclusively with mode `0600`; its expected contents are empty. The collector refuses to read and hash this path if metadata reports a nonzero size, recording a policy mismatch instead. The digest is therefore for an empty byte sequence and entails no content read. If the installed workflow changes the lock file's contents or role, this exception must be reviewed before use.

All other production files are metadata-only, regardless of filename, size, or discovery depth. In particular, the installed runner, gateway support code, sudoers material, canary receipt, backups, logs, credentials, secrets, and unknown recursively discovered files are never content-read. Hash byte and individual-file limits are applied only to authorized hash paths. Synthetic tests may add one explicitly named fixture under an allowlisted root to exercise bounded nonempty hashing; the test-only environment is refused in production mode and synthetic mode is forbidden as root. The global entry, recursion-depth, and output limits still bound inventory collection.

## Output-directory policy and operator preflight

The destination must be a new absolute normalized path. Parent traversal starts at `/` and opens each component relative to the already-open parent descriptor with `O_DIRECTORY|O_NOFOLLOW`. Production parent components must be root-owned and not group/world writable; the final parent must also satisfy that rule. The new output directory is created relative to the verified parent with mode `0700` and re-opened without following links. Files are created relative to the output descriptor with `O_EXCL|O_NOFOLLOW`, mode `0600`; file and directory descriptors are fsynced and closed.

Do not assume `/mnt`, `/var/tmp`, or any mount is suitable. On the authorized Debian VM, run a separate read-only preflight that checks the existing `/var/lib` directory's owner, mode, ACLs, mount/filesystem, available space, and that a unique sibling name such as `/var/lib/athena-alpha-m1-evidence-<UTC-run-id>` does not exist. `/var/lib` is only a candidate: the preflight must establish its actual safety and suitability on the VM. Do not create an administrative parent directory to make the collector succeed. If no suitable existing root-owned parent is found, acquisition is NO-GO until an independently reviewed destination is designated.

Output is intentionally kept outside the repository, and the output directory is not one of the collected roots. The collector does not write to a caller-selected existing destination.

## VM identity checks

Production mode retains the M28.1 identity gate: Linux; hostname `debian`; Debian 13 `/etc/os-release`; machine ID `9bdbfd8678b5467cb85e4d8aa5e01fea`; product serial `VirtualBox-e475ea91-72e9-4157-9ee6-583e9ae0462f`; nonempty product UUID and boot ID; and an Oracle/Innotek system vendor. Identity inputs are opened without following their final symlink and must be regular files. A mismatch aborts before collection. Synthetic identity values are available only with `MONAD_M28_2_TEST_ROOT`, and synthetic mode explicitly refuses effective UID 0.

These checks identify the expected VM but do not authenticate the host or establish a trusted boot chain. Production identity was not evaluated in this session.

## Filesystem safety and races

Each path component is opened descriptor-relative with no-follow semantics. Entries are classified with `stat(..., follow_symlinks=False)` before opening. Symlinks and special files, including FIFOs and sockets, are recorded and never opened for content access. Regular files and directories are opened with no-follow and nonblocking flags, then compared against the classified device, inode, and file type. A mismatch is recorded as an identity error.

Regular-file metadata is captured before and after authorized hashing; size, inode, device, mtime, and ctime changes produce a partial result. Directories use bounded name snapshots and before/after metadata and name comparisons; changes and traversal errors are recorded locally. Failed reads, open errors, inaccessible paths, missing intermediate directories, and targets disappearing after traversal begins have distinct statuses. An absent target is optional only when its direct parent exists and is accessible.

Acquisition is not atomic. An attacker or system process can change entries between metadata checks, during traversal, or after the final check. Descriptor-relative access and identity comparisons reduce race exposure but do not eliminate TOCTOU, provide a filesystem snapshot, or guarantee a globally consistent point-in-time view. The tool reports observed changes; absence of a change report is not proof that no transient change occurred.

## Resource bounds and completeness

Defaults are depth 32, 10,000 entries, 256 MiB total authorized hashing, 64 MiB per authorized hashed file, and 8 MiB output. Hard caps are depth 128, 100,000 entries, 2 GiB total authorized hashing, 512 MiB per file, and 32 MiB output. Directory snapshots stop at the remaining entry budget, including their post-traversal snapshot; inventory records and path names are consequently bounded by the entry cap and filesystem component limits. Metadata-only file size does not block inventory.

Any access/traversal/hash error, symlink, special file, filesystem identity issue, mutation, or resource limit makes completion `PARTIAL`. Expected absence alone does not. If the full document exceeds the output cap, the collector attempts a small `PARTIAL` report that names the output-size limit, expected path scope, and number of omitted records. If even that minimal record cannot fit or output writing fails after directory creation, it preserves the created destination and exits with an error; the operator must preserve and review that directory rather than treating it as evidence of a complete acquisition.

`COMPLETE` means every allowlisted path was inventoried or legitimately absent and no detected issue or configured limit prevented the walk. It does not mean atomicity, forensic soundness against a hostile live kernel, or release qualification.

## Verification evidence

Commands executed:

- `bash -n qualification/m28-2-readonly-acquisition.sh` — PASS.
- `python3 -m py_compile tests/m28_2_readonly_acquisition_test.py` — PASS.
- `python3 tests/m28_2_readonly_acquisition_test.py` — 16 PASS, 1 SKIP, 0 FAIL (17 tests total).
- `shellcheck` — SKIP: not installed in this environment.
- `git diff --check` — PASS for tracked diff; new deliverables are untracked and therefore not covered by Git's ordinary diff check. The files were also reviewed through the executed syntax and test checks.

Synthetic coverage includes valid collection and checksum verification; optional absence versus missing intermediate paths; concurrent disappearance; symlink target, ancestor, and descendant; FIFO; socket (skipped by sandbox restriction); large metadata-only file; exact allowlisted hashing; proof by result metadata that unauthorized files have no digest; injected hash read error; file and directory mutation; entry/depth/hash-byte/output limits; existing destination; unsafe and symlinked output parents; injected identity mismatch; synthetic fixture path remapping; and failure after output creation. Tests use temporary fixtures only and never invoke production collection against host administrative paths.

The root-only synthetic-mode rejection is enforced directly in the implementation but was not dynamically tested because this suite is designed to run unprivileged. Permission-denial races and genuine kernel-level identity swaps are not safely inducible in this unprivileged synthetic run; their error paths were inspected, while deterministic disappearance, mutation, and identity mismatch paths use test-only injections.

## Descriptor and exception review

Opened traversal descriptors are closed in `finally` blocks; chain-open failures close the currently held descriptor. File-write descriptors are closed even after a write/fsync exception, and the output directory descriptor is closed in the outer `finally`. Errors are caught at the path, traversal, hash, and output boundaries and represented as partial evidence or a nonzero post-creation failure. No blanket exception handler silently marks a failed path successful. The synthetic test switches are rejected in production mode. Output write failure may leave one or more exclusive files in the preserved directory; that state is intentionally not cleaned up.

## Remaining limitations and readiness

- This is a live, non-atomic metadata acquisition; see race limitations above.
- Production VM identity and destination-parent suitability were not checked here.
- Unix socket fixture execution is unverified in this sandbox due to `EPERM`; rerun that synthetic test in an unprivileged environment that permits AF_UNIX sockets.
- ShellCheck was unavailable.
- Output checksum is a local integrity aid, not an authenticated signature or chain of custody.
- The collector's source has not been independently reviewed or pinned/promoted through the project's security-sensitive promotion workflow.
- No privileged acquisition, approved Debian VM campaign, evidence review, or Alpha M1 release qualification occurred.

**Acquisition readiness: implementation ready for independent source review and read-only VM preflight only. Privileged acquisition: NO-GO pending explicit review and authorization. Alpha M1 release qualification: NOT CLAIMED.**

## Exact next review action

Have an independent security reviewer inspect the new collector and this report, with special attention to output-parent selection, the empty-lock-file hash exception, filesystem race handling, and the test-only hooks. After that review, perform a read-only preflight on the authorized Debian VM to verify identity and nominate an existing safe output parent. Record both results and obtain explicit authorization before any privileged acquisition is considered.
