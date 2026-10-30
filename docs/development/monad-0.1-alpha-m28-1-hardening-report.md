# Monad 0.1 Alpha M28.1 forensic acquisition hardening

## Recommendation

**NO-GO for privileged acquisition pending explicit implementation review.** The new script passed its synthetic checks in this development environment, but those checks do not establish production VM identity, privileged filesystem behavior, ACL behavior, mount behavior, or resistance to a hostile concurrent writer. Do not invoke it with privilege until an independent reviewer has inspected the implementation and the target VM preflight has been performed through the approved process. No acquisition was executed during this work.

## Threat model

The acquisition process is read-only with respect to its selected source trees, but reads privileged administrative evidence and creates a new evidence directory. The threat model includes malicious or accidental symlinks, special files, path substitution, concurrent file and directory changes, mount crossings, inaccessible entries, oversized inventories, unsafe output parents, pre-existing output, identity confusion, and environment-based path remapping. Source trees and their contents are treated as untrusted. The JSON and checksum are locally generated records, not authenticated provenance.

## Implementation decisions

`qualification/m28-1-readonly-acquisition.sh` is a new standalone implementation; the original M28 script, test, and runbook were not modified. It uses a fixed literal production allowlist and invokes Python 3 for descriptor-relative filesystem handling. Roots and descendants are classified with `stat(..., follow_symlinks=False)` before opening. Directory components are walked from `/` with `O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC`; entry opens add `O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC`, and directory opens also use `O_DIRECTORY`. Opened device, inode, and type are compared with the prior classification. Symlinks and special files are never opened for content. Filesystem device changes are recorded as `FILESYSTEM_BOUNDARY` and not traversed.

Regular eligible files are SHA-256 hashed in bounded chunks. No file contents are copied. Runner, support module, sudoers, and canary executable contents are deliberately not opened for hashing. Other ordinary regular files are eligible for metadata plus hash evidence. Every opened file descriptor is closed; output file descriptors use exclusive creation, mode 0600, flush and fsync. The output directory is mode 0700 and fsynced after both files are created.

The output must be a new normalized absolute path outside `/etc`, `/usr`, `/var`, `/run`, `/root`, `/opt`, and the repository. The output parent chain is opened component by component without following symlinks. The final parent must be owned by the effective user and not group/world writable. Production mode rejects `MONAD_M28_1_*` environment settings; synthetic path remapping is available only for non-root test runs.

## Exact administrative allowlist

The list below is fixed in the script. It is based on the current installer source and the prior M28 acquisition plan; duplicate or overlapping roots are intentionally retained because each named administrative concern is independently visible in the result.

1. `/usr/local/sbin/athena-alpha-m1-runner` — installed runner; content not hashed.
2. `/usr/local/sbin/gateway_support.py` — installed support module; content not hashed.
3. `/etc/sudoers.d/athena-alpha-m1` — installed sudoers file; content not hashed.
4. `/var/lib/athena-alpha-m1/admin-install.lock` — installer serialization lock.
5. `/var/lib/athena-alpha-m1` — complete administrative state root, receipts, archives and unknown historical entries.
6. `/var/lib/athena-alpha-m1/backups` — installer backup transaction trees and markers.
7. `/root/athena-alpha-m1-stage` — installer staging source tree, including candidate executable files; sensitive known executable names are not hashed.
8. `/run/athena-alpha-m1` — volatile runner locks, staging, run directories and manifest metadata.
9. `/var/log/athena-alpha-m1` — administrative logs.
10. `/var/lib/athena-alpha-m1/systemd-canary.json` — current canary receipt.
11. `/var/lib/athena-alpha-m1/ext4-fixture` — D6 fixture/residual parent; historical fixture contents are preserved and read-only inventoried.
12. `/var/tmp/athena-d67-ext4` — D6 parent reported by the M28 source runbook.
13. `/opt/athena-qualification/build` — protected build artifacts; inventory is bounded and executable names are excluded from content hashing where recognized.

The installer source declares its stage, runner, support, sudoers, log, fixture, backup, receipt and transaction lock locations in `qualification/install-athena-alpha-m1-admin.sh`. `qualification/athena-alpha-m1-runner.py` supplies the runtime `/run` names. The canary source supplies archived receipt naming. The state-root and recursive backup walks preserve unknown and historical entries. The acquisition does not assume marker contents prove transaction success.

## VM identity

Production requires Linux, hostname `debian`, Debian GNU/Linux 13, machine ID `9bdbfd8678b5467cb85e4d8aa5e01fea`, exact hardware serial `VirtualBox-e475ea91-72e9-4157-9ee6-583e9ae0462f`, a nonempty product UUID, a nonempty boot ID, and Oracle/Innotek DMI vendor evidence. It records hostname, machine ID, serial, product UUID, boot ID, kernel release and UTC timestamp. Boot ID is recorded but not pinned. Synthetic tests use explicit synthetic identity values and cannot run as root. Production identity was not established in this session.

## Race handling and filesystem boundaries

The scanner records regular-file metadata before and after hashing and marks a changed file `CHANGED` if device, inode, size, mtime or ctime differs. It records directory metadata before and after traversal and compares sorted entry-name snapshots to detect additions/removals. Snapshotting uses `os.scandir` and is bounded by the remaining entry budget; an oversized directory is reported as an entry-limit error without collecting its full name list. Disappeared/unopenable entries become errors; a classification/open identity mismatch is `CHANGED`. Each allowlisted root establishes its own device and Linux mount-ID boundary; a child on another device or mount ID is reported and not traversed where mount ID is available.

These checks are not an atomic snapshot. A sufficiently timed attacker may change content and restore selected metadata; directory changes between snapshots may also escape observation. The implementation detects mount crossings by mount ID when `/proc/self/fdinfo` provides it, but does not constitute a kernel-enforced snapshot or prevent mount changes racing with traversal.

## Resource limits

Conservative production defaults are depth 32, 10,000 entries, 256 MiB total hashed bytes, 64 MiB per file, and 8 MiB serialized JSON. Hard ceilings are depth 128, 100,000 entries, 2 GiB total, 512 MiB per file, and 32 MiB output. Synthetic tests may lower/adjust limits within those ceilings. On an exceeded traversal limit, the affected path and limit are reported as an error and the result is `PARTIAL`. If the serialized inventory exceeds the output ceiling, the script writes a compact `PARTIAL` document that explicitly reports omitted inventory record count; it does not claim a complete inventory. If even that record cannot fit, output is incomplete and the command exits 2, preserving any already-created directory.

## Output protection

Output files are `inventory.json` and `SHA256SUMS`, created with `O_EXCL` and mode 0600. They are flushed and fsynced, then the directory is fsynced. Existing output is rejected and never overwritten. Parent symlinks, unsafe final-parent ownership/modes, protected administrative trees and repository output are rejected. Operators must choose a new private destination on a trusted removable or staging filesystem, then retrieve only the newly created evidence directory through the approved VM export mechanism. Do not copy a broader administrative parent, expose the VM's `/root` or `/var/lib` trees, or stage evidence within the protected trees. Preserve partial evidence as created; do not retry into it.

## Completeness semantics

Top-level roots use `PRESENT`, `ABSENT`, `SYMLINK_NOT_FOLLOWED`, or `ERROR`. Recursive entries use `PRESENT`, `SYMLINK_NOT_FOLLOWED`, `FILESYSTEM_BOUNDARY`, `CHANGED`, or `ERROR` (including special files not opened and limit violations). `ABSENT` optional paths are acceptable. `COMPLETE` requires every selected path to be fully inventoried with no errors, symlinks, boundaries, mutation, or resource limit. Any traversal error, symlink, boundary, change or resource limit makes the acquisition `PARTIAL` and returns 1. Preflight or output failures return 2. COMPLETE means only that this bounded scan observed no listed incompleteness; it does not prove authenticity, atomicity, correctness of installed behavior, or qualification.

## Verification results

Commands executed:

- `bash -n qualification/m28-1-readonly-acquisition.sh` — PASS.
- `python3 -m py_compile tests/m28_1_readonly_acquisition_test.py` — PASS.
- `python3 tests/m28_1_readonly_acquisition_test.py` — 20 tests: **17 PASS, 0 FAIL, 3 SKIP**.
- `shellcheck qualification/m28-1-readonly-acquisition.sh` — NOT RUN if ShellCheck is not installed; see final session record.
- `git diff --check` — recorded in final session record.

The skipped cases are precise environment limitations: AF_UNIX bind is denied by the sandbox; mode-000 permission denial is bypassed by the effective test identity; and root-only synthetic-mode refusal cannot be exercised because this test runner is non-root. The script contains the explicit root refusal. Synthetic fixtures were the only fixtures used. No privileged acquisition, installer, runner, canary, collector, or privileged test driver was invoked.

## Known limitations and residual risks

- No atomic snapshot; metadata comparison is detection, not transactional consistency.
- Identity and metadata inputs are read from the live VM; source authenticity is not established by the output checksum.
- Directory fsync and descriptor semantics vary by filesystem and kernel; target VM behavior has not been validated.
- The output parent validation checks every component against symlink traversal, rejects writable non-sticky ancestors, and requires the final parent to be owned by the effective user without group/world write access. Sticky shared ancestors such as `/tmp` are allowed; operator path selection still matters.
- Build-tree and stage content classification is name-based for known sensitive executable names. It does not copy contents, but ordinary regular-file hashes can reveal equality with known data.
- AF_UNIX handling, real permission denial and root refusal remain unexercised in this sandbox.

## Operator runbook

1. Obtain explicit independent review of the M28.1 script, allowlist, output policy, limits and residual risks. Do not treat this report or synthetic tests as authorization.
2. Follow the approved Alpha M1 Debian VM process. Confirm hostname, OS, machine ID, hardware serial and VirtualBox identity through trusted preflight. Do not alter system identity to make checks pass.
3. Select a new absolute normalized output directory on a trusted, private destination outside all protected trees and the repository. Ensure its existing parent is owned by the acquisition identity and is not group/world writable.
4. Invoke the script only after review and approved authorization, with exactly that new destination. Preserve exit status and all output. Exit 0 means observed COMPLETE, exit 1 means PARTIAL, and exit 2 means preflight/output failure.
5. Inspect the inventory's completion and every absent/issue record. Preserve a partial directory as evidence; never rerun into or overwrite it.
6. Retrieve only the created evidence directory using the approved export mechanism, retain its checksum file, and record chain-of-custody details separately. Do not broaden collection to unrelated administrative data.

This report documents engineering work, not an approval to conduct acquisition.
