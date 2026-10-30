# Monad 0.1 Alpha M34.2 — independent security audit of M34.1

**Verdict: `NO_GO_REQUIRES_FIXES`. Privileged acquisition: NOT AUTHORIZED. Alpha M1: NO-GO / NOT QUALIFIED.** The declared M34.1 digest matches, but a synthetic adversarial case makes the collector return `COMPLETE` when a required regular-file target is a directory. No production acquisition or administrative path was accessed.

## 1. Executive summary

M34.1 uses several sound defensive techniques: fixed source roots, descriptor-relative traversal, `O_NOFOLLOW`, rejection of special files and hard links, inode checks around opens, bounded reads/enumeration, private exclusive output creation, and hashes over copied output. The original 22-test suite passes. The independent suite confirms the common symlink, replacement, mutation, limit, output, and checksum checks.

One confirmed HIGH finding invalidates the completion signal: required file paths are accepted whenever their inventory row says `PRESENT`; the code does not require a regular file or captured bytes. Reproduction replaces the synthetic runner file with a directory; M34.1 exits 0 and emits `completion: COMPLETE`. A second, lower risk is that filesystem boundaries are compared by `st_dev` only, so same-device bind mounts are not distinguished. Finally, the raw-copy rule accepts any direct child of the named backup ending `.absent` or `.existed`, broader than the documented known-marker list.

The latter two findings do not explain or establish any real VM state. This audit does not authenticate the collector or its output and does not qualify the VM.

## 2. Scope and methodology

Reviewed in full:

- `qualification/m34-1-targeted-forensic-acquisition.sh`
- `tests/m34_1_targeted_forensic_acquisition_test.py`
- `docs/development/monad-0.1-alpha-m34-1-acquisition-review.md`
- `docs/development/monad-0.1-alpha-m33-forensic-review.md`
- `docs/development/monad-0.1-alpha-m34-review.md`
- `qualification/m32-1-readonly-acquisition.sh` as historical context

The analysis treated the invoking interpreter and collector bytes as trusted only for the local run, as M34.1 itself assumes. The threat model included an unprivileged local user, concurrent changes, hostile names and file types, symlink/hardlink attacks, resource exhaustion, interrupted output, and administrative or namespace-created mounts. Tests created source trees and outputs only in temporary directories. No `sudo`, privileged test, real VM, other-user path, installation, rollback, canary, mount, or system service action was used.

## 3. Baseline and hashes

Initial Git state: branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`. The workspace already had many modified tracked files, untracked documentation and qualification artifacts, build directories, and test cache material. All pre-existing state was preserved. The two authorized output paths did not exist before this audit. M34.1 and its original test were not modified.

| File | SHA-256 | Result |
|---|---|---|
| `qualification/m34-1-targeted-forensic-acquisition.sh` | `053b4375cae4342e160b289fc9781a66b6a64dafae67be15dea29ff95314ffc9` | PASS; matches declared digest |
| `tests/m34_1_targeted_forensic_acquisition_test.py` | `c52bc8c5b8b2f947f081805de62e6455e0cbfa862ce684be54cf0ad0ea033d50` | Measured |
| `tests/m34_2_security_audit_test.py` | `fbed6b9c38f456d166212b031f29b98259fff1746a411df7af3fd16d82fe0d79` | Created by this audit |

Hash equality proves byte identity with the declared digest, not provenance, approval, or that these bytes were used for any previous acquisition.

## 4. Threat model

An unprivileged local user can create hostile names, symlinks, FIFOs, hardlinks to files they can access, and race changes in directories writable to them. Processes with write access to an observed tree can replace names or mutate files during a live walk. ACLs and access modes can make paths unreadable. Disk-full, I/O errors, process termination, and resource pressure can interrupt output.

Ordinary users generally cannot alter root-owned administrative trees or install host mounts. A privileged actor, a pre-existing bind mount, or an applicable user/mount namespace can change that assumption. Such mounts are in scope because a root collector can traverse a mounted tree beneath an allowed pathname. A malicious kernel, compromised Python interpreter, or privileged actor able to rewrite evidence and metadata remains outside the collector's protection. The collector also assumes its own exact bytes are trusted; a local digest by itself supplies no external trust anchor.

## 5. Static analysis

### Path handling and descriptors

Production paths are rooted at `/` and traversed by directory descriptors with `O_DIRECTORY|O_NOFOLLOW`; leaf opens use `O_NOFOLLOW|O_NONBLOCK`. `fstat` is compared with a prior no-follow `stat`. This prevents ordinary symlink traversal and blocking on a FIFO. Absolute output paths must be normalized and outside configured source roots and the repository. Parent components are opened without following symlinks; the destination is created with `mkdirat` semantics, mode `0700`, and existing destinations fail. Output files use `O_EXCL|O_NOFOLLOW`, mode `0600`, and descriptor-relative writes.

The synthetic test mode has explicit safeguards: it is refused for effective UID 0, requires a caller-owned private root, and production mode refuses `MONAD_M34_*` controls. Synthetic hooks mutate only the synthetic tree. These controls are not a substitute for trusting the collector bytes in production.

### Type, owner, permissions, and hard links

Only regular files and directories are opened. Symlinks and special files create issues. Production source objects must be UID 0; writable group/world bits are rejected, and read/search bits are required where data is read or directories enumerated. Regular files require `nlink == 1`. GID is recorded, not pinned. ACLs and xattrs are not collected, so the mode check is not a complete record of effective access policy.

### Mount boundaries and traversal

The recursive walk compares `st_dev` with the starting device at lines 192–193. It does not obtain a mount ID. A bind mount from the same underlying device can preserve `st_dev` and evade this boundary check. Each configured root also starts with a fresh boundary. This requires a mount arrangement that an ordinary attacker usually cannot create in the host namespace, but it matters for privileged acquisition and pre-existing mount topology.

Depth, entry count, enumerated names, per-file bytes, total bytes, and inventory size have hard limits. The traversal retains sorted names and captured content in memory, but those structures are bounded by the stated limits. A limit/error produces an issue and ordinarily prevents `COMPLETE`. There is no wall-clock deadline inside the collector. Abrupt termination such as SIGTERM can leave a created output directory without a final inventory; SIGINT is handled only after output creation and is converted to `BLOCKED` where execution reaches report generation.

### Content allowlist and reads

The source perimeter is fixed to five configured roots. Content is copied for exact named files and selected direct backup children. `hash_allowed()` hashes all regular files beneath the named backup transaction, even when they are not copied; the documented M34.1 intent describes this. However, `content_allowed()` also accepts any direct backup name whose suffix is `.absent` or `.existed`, not only known markers. A misleading or attacker-added name inside that directory can therefore be copied as raw evidence. This is limited to the named backup tree, but can disclose unintended bytes and exceeds the narrow-name claim.

File data is accumulated in memory, hashed, then compared against pre-read and post-read size/timestamps and the currently named inode. Common growth, truncation, mutation, unlink, and replacement cases are detected. A live acquisition is still non-atomic: a mutation after the final check is outside those checks. Hashes identify bytes read; they do not prove source, author, collector identity, or time.

### Inventory, completion, checksums, and output

The M32.1 source checksum is parsed as exactly one `inventory.json` line and checked against copied bytes. Output `SHA256SUMS` contains hashes for `inventory.json` and copied payloads. This detects later mismatch when verified independently; it is unsigned and does not authenticate its own origin.

Required paths are checked at lines 296–302, but the check accepts any row with status `PRESENT`. The walk uses that status for both regular files and directories. This is a real gap in the COMPLETE invariant (Finding M342-01). Output files are written and fsynced, then the output directory and parent are fsynced and names rechecked. Failure after creation may leave a partial destination; the caller is told to preserve it. SHA256SUMS itself is not included in its own digest list, which is normal but means its integrity needs an external anchor.

## 6. Adversarial tests and validation

Independent tests: **18 PASS, 3 SKIP, 0 FAIL** (21 test methods; some methods cover multiple vectors). The confirmed false-COMPLETE case is intentionally a passing regression reproducer: it asserts the unsafe behavior so the report captures it. It is not a security pass for that behavior.

Original M34.1 suite: **22 PASS, 0 FAIL, 0 SKIP**.

Additional checks: `bash -n` PASS; compilation of the embedded Python with `compile()` PASS; declared M34.1 digest PASS. `shellcheck` was unavailable and is SKIP. The M34.2 test cache artifact generated by Python's test import was removed; no generated audit bytecode is retained.

| Requested vector | Result / evidence |
|---|---|
| Final and intermediate symlink | PASS; not followed |
| Directory and file replacement | PASS; inode mismatch reported |
| Mutation and expansion during acquisition | PASS through synthetic mutation hook; changed metadata reported |
| Truncation during read | SKIP; no deterministic truncate hook, scheduling race unreliable |
| Special file and FIFO | PASS; recorded without open |
| Hostile directory permissions | PASS; partial result |
| Existing output; symlink/writable output parent | PASS; refused, existing sentinel preserved |
| Path traversal and hostile filename | PASS; traversal refused; newline filename safely serialized and inventoried |
| Depth, enumeration, per-file and total-byte limits | PASS; partial result |
| Output write failure | SKIP; no reliable ENOSPC/EIO injection or dedicated fault filesystem in sandbox |
| Controlled interruption and partial manifest | PASS; `BLOCKED`/`PARTIAL` recorded |
| False `COMPLETE` | Finding reproduced; PASS as regression reproducer, unsafe collector result |
| Wrong source hash, post-acquisition edit, checksum inconsistency | PASS; independent digest comparison detects mismatches |
| Same-device bind mount | SKIP; mount creation/change was outside authorization; static gap confirmed |

Minimal false-COMPLETE reproduction: create an otherwise valid private synthetic root with all required leaves and a consistent M32.1 inventory/checksum; replace `usr/local/sbin/athena-alpha-m1-runner` with an empty directory; run the collector in synthetic mode to a new private temporary output. Observed exit code is 0, target row status is `PRESENT`, and inventory completion is `COMPLETE`.

## 7. Findings

### M342-01 — Required regular-file targets can be directories while completion is `COMPLETE`

- **Severity:** HIGH
- **Location:** `qualification/m34-1-targeted-forensic-acquisition.sh:296–305`; required target definitions at lines 10–13.
- **Prerequisites:** A required file pathname resolves to a directory (through pre-existing corruption, privileged change, or concurrent replacement); other required leaves and source checksum remain valid.
- **Scenario:** Replace the synthetic runner file with a directory, then invoke the collector on a valid fixture. The generic walker reports `PRESENT`; required validation accepts that status without checking file type or `content_status`.
- **Impact:** False claim of complete required evidence. An operator relying on exit 0/`COMPLETE` may proceed without runner bytes/hash. Same logic applies to `gateway_support.py` and the sudoers target. It does not by itself grant privilege, but undermines a forensic gate.
- **Reproducibility/evidence:** Confirmed by `test_required_regular_file_directory_substitution_can_false_complete`; exit 0 and `COMPLETE` observed.
- **Mitigation:** Encode an expected type for every required path. Require the backup path to be a directory; require runner/support/sudoers/inventory/SHA256SUMS leaves to be regular files with the expected captured content and digest fields. Make every mismatch an issue before computing completion.
- **Test:** M34.2 required-directory substitution reproducer.

### M342-02 — Same-device bind mounts evade the filesystem boundary check

- **Severity:** MEDIUM
- **Location:** `qualification/m34-1-targeted-forensic-acquisition.sh:192–193`, with each root walk initialized at lines 267–274.
- **Prerequisites:** A same-device bind mount exists under an allowed root, created by an administrator or permitted mount namespace.
- **Scenario:** Bind-mount a directory outside the intended acquisition subtree beneath an allowed root. Since `st_dev` can be unchanged, recursive descent continues and can enumerate/hash/copy content according to the path allowlist.
- **Impact:** Collection may cross the intended mount boundary and include unrelated data. This is not normally achievable by an unprivileged host user without mount capability; it is relevant to a privileged collector and preconfigured mount topology.
- **Reproducibility/evidence:** Static confirmed gap; not executed because no mount operation was authorized or needed for the sandbox tests.
- **Mitigation:** Record and compare mount IDs (`statx` or `/proc/self/fdinfo` anchored to the descriptor), and define whether each root's initial mount is allowed. Treat unavailable mount identity as a blocking issue.
- **Test:** Mount test NOT RUN (environment/authorization limit).

### M342-03 — Suffix-based backup names can be copied outside the named marker list

- **Severity:** LOW
- **Location:** `qualification/m34-1-targeted-forensic-acquisition.sh:50–53`.
- **Prerequisites:** An unexpected direct child exists in the named backup directory with a name ending `.absent` or `.existed`.
- **Scenario:** Place a file such as `private-not-a-marker.absent`; it is accepted for raw capture even though its basename is not in `CONTENT_NAMES`.
- **Impact:** Unintended evidence disclosure and weaker adherence to the stated explicit-name copy policy. Scope is confined to the one backup directory; source writes are not made by the collector.
- **Reproducibility/evidence:** Confirmed statically. No adversarial secret fixture was added because current M34.2 tests focused on traversal and completion behavior; this can be added as a targeted regression case in M34.3.
- **Mitigation:** Remove suffix wildcard acceptance and list every approved marker basename exactly; report other backup children as hash-only or metadata-only according to the approved policy.
- **Test:** Static rule inspection; additional focused test recommended.

## 8. Limits and blocked verification

No VM-side identity, ACL/xattr, actual mount topology, privileged source permission behavior, kernel/filesystem crash behavior, or provenance was verified. Truncation mid-read and forced output I/O failure were SKIP for lack of deterministic unprivileged injection. A malicious privileged actor can alter sources and outputs outside the protection model. No actual collector execution against production paths occurred.

## 9. Risk matrix

| Finding | Severity | Likelihood in stated ordinary-user model | Impact | Status |
|---|---|---|---|---|
| M342-01 false `COMPLETE` on required directory | HIGH | Low to medium; requires malformed or changed required path | High for evidence gate | Confirmed |
| M342-02 same-device bind mount | MEDIUM | Low for ordinary user; higher with pre-existing/admin mount | Scope escape / unrelated data collection | Static confirmed, runtime blocked |
| M342-03 suffix wildcard copy | LOW | Low; requires unexpected backup entry | Unintended raw capture | Static confirmed |

## 10. Mitigations proposed for M34.3

1. Add explicit expected-type and required-content policy for all required paths; calculate completion only after those typed predicates pass.
2. Enforce mount-ID boundaries and document per-root allowed mount identities; fail closed if mount identity cannot be obtained.
3. Replace suffix-based raw-copy authorization with exact marker names; add a test showing a suffix lookalike is not copied.
4. Add deterministic truncate and output-write fault injection that is only available under a private non-root synthetic root; do not add test controls to production mode.
5. Consider graceful handling for termination signals and record a durable partial-state marker where possible; document that SIGKILL/power loss can always leave an incomplete directory.
6. Independently verify the output tree and the exact collector digest before any future authorized acquisition. Preserve the distinction between digest integrity and provenance.

## 11. GO / NO-GO criteria

M34.1 is **NO-GO for operator review as an acquisition-ready artifact** until M342-01 is fixed and retested. M342-02 should be resolved or explicitly accepted by a future independent review before privileged acquisition; M342-03 should be narrowed to match the documented allowlist. Any future privileged acquisition still requires a separate explicit authorization naming the guest, exact reviewed bytes, read scope, output destination, and custodian. This audit grants none.

## 12. Verdict

**`NO_GO_REQUIRES_FIXES`.** The hash baseline and broad defensive design are sound enough to support a focused M34.3 remediation, but the confirmed false `COMPLETE` condition makes the current collector unsuitable as a completion gate. No privileged acquisition is authorized. Alpha M1 remains **NO-GO / NOT QUALIFIED**.
