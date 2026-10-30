# Monad 0.1 Alpha M34.4 — independent remediation verification

**Verdict: `GO_FOR_OPERATOR_REVIEW`. Alpha M1 remains `NO-GO / NOT QUALIFIED`. Privileged acquisition is not authorized.** This review used synthetic temporary trees and unprivileged local checks only. It did not execute the collector against production paths or create mounts.

## 1. Executive summary

The M34.3 changes remediate the three M34.2 findings in the tested code paths. Required object type/content predicates prevent the previously reproduced false `COMPLETE`; descriptor-bound `statx(STATX_MNT_ID)` is correctly requested and read on this Linux x86_64 environment, with failures classified fail-closed; exact marker basenames alone are copied. The independent suite reports **15 PASS, 0 FAIL**. M34.1 reports **26 PASS** and M34.3 **11 PASS**.

The unchanged M34.2 suite reports **17 PASS, 1 FAIL (expected historical assertion), 3 SKIP**. Its failing test requires the original unsafe result (`exit 0` and `COMPLETE` for a required runner replaced by a directory). The independent test verifies instead that the result row is `TYPE_MISMATCH`, carries `expected_type: file`, records a required type issue, and the global state is `PARTIAL` with exit 1.

No confirmed new vulnerability was found in the reviewed/tested local paths. Dynamic bind-mount protection remains **unverified**: mismatch behavior was exercised using the collector's synthetic hook, and no real mount was created. The configured source root's initial mount is accepted as its own boundary; this policy cannot identify an unintended mount already attached exactly at a configured root. This must be reconciled against independently reviewed guest mount topology before any acquisition decision.

## 2. Baseline and digests

Before testing, Git was recorded as branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`, tracking `origin/main`. The worktree contained many pre-existing modified tracked files, untracked documentation, qualification material, build trees, and cache artifacts. These were preserved. Both authorized destination paths were absent at baseline. No prior M34.1, M34.2, M34.3 code or documents were edited.

| Artifact | SHA-256 | Baseline/result |
|---|---|---|
| M34.3 collector `qualification/m34-1-targeted-forensic-acquisition.sh` | `56771879aa73bbdcac4368c9babc990f73150401b6aecfc8cb2646263c34d9d9` | PASS; required digest matches |
| M34.1 suite | `938254e4cd8cf53df0e1b4051c7a8febb6a70ac189733196f6d2351b9d94f4f8` | Measured; unchanged |
| M34.2 historical suite | `fbed6b9c38f456d166212b031f29b98259fff1746a411df7af3fd16d82fe0d79` | PASS; required unchanged digest matches |
| M34.3 regression suite | `ea8183a31ead5e1ec20980d7d74d3eeb4ecd5dddf117a73a21f9dd6a69654fcb` | Measured; unchanged |
| M34.2 audit report | `a159cb3ac08e14aa2c2e1c6a9a32b4ebbdaf526c2c536321cfce7f33169abe21` | Measured; unchanged |
| M34.3 remediation report | `baaa5e55a0d2254c408408f2815a56b329234b95380724803fc42e47d707c3c9` | Measured; unchanged |
| New M34.4 test | `f7efb8359b12ea54c0f78b3ff261542ffaf72b883c8d94671b241e52c384e833` | Created by this review |

Digest equality establishes local byte identity with the declared values, not origin, approval, or past execution. M34.1/M34.2 contract documents were consulted, as were the local M33 and M34 acquisition reviews; their VM observations remain reported context rather than newly authenticated evidence.

## 3. Methodology

The review inspected the final collector and M34.2/M34.3 reports, compared the implemented predicates against the documented M34 acquisition perimeter and explicit marker policy, and added an independent synthetic test suite. Tests use caller-owned temporary source/output directories, parse generated manifests, inspect copied payloads and independently verify output checksums. No production source path is traversed. No administrative acquisition, `sudo`, install, rollback, canary, systemd operation, mount operation, or privileged test was run.

The `statx(2)` interface documents `AT_EMPTY_PATH` as descriptor-relative operation and `STATX_MNT_ID` as the requested mount ID field. The collector passes both flags as `0x1000`, checks the returned mask, and reads `stx_mnt_id` at byte offset 144 in a 256-byte buffer. These constants and layout match the Linux UAPI on the tested x86_64 Linux 6.12 kernel. A separate test called libc `statx` on an opened temporary directory and observed the requested mask bit and a nonzero ID. See [Linux statx(2)](https://man7.org/linux/man-pages/man2/statx.2.html) and [Linux UAPI stat.h](https://github.com/torvalds/linux/blob/master/include/uapi/linux/stat.h).

## 4. HIGH finding verification

The completion gate now checks each required path against an expected type. Required files must be `PRESENT`, have their exact captured-content status, and carry a SHA-256. The required backup transaction must be a directory. Missing entries, wrong types, permission/open/stat/enumeration errors, source checksum errors, and controlled interruptions add issues or block completion before the final classification. `COMPLETE` is computed only when the issue list is empty.

| Case | Manifest evidence | Classification / exit | Result |
|---|---|---|---|
| Required runner replaced by directory | `TYPE_MISMATCH`, `expected_type: file`, `REQUIRED_TYPE_MISMATCH` issue | `PARTIAL`, 1 | PASS |
| Required backup directory replaced by file | `TYPE_MISMATCH`, `expected_type: directory` | `PARTIAL`, 1 | PASS |
| Required file absent | `REQUIRED_ABSENT` or equivalent missing-required row plus `REQUIRED_EVIDENCE_UNAVAILABLE` | `PARTIAL`, 1 | PASS |
| Required source inaccessible | Permission/access issue and required evidence unavailable | `PARTIAL`, nonzero | PASS |
| Required bytes/digest unavailable | Required content unavailable or non-present row; no complete digest predicate | `PARTIAL`, nonzero | PASS |
| Incomplete/bad source checksum | `SOURCE_CHECKSUM_INVALID` | `PARTIAL`, 1 | PASS |
| File replacement/mutation during read | `IDENTITY_CHANGED`, `PATH_REPLACED_DURING_ACQUISITION`, or `CHANGED_DURING_ACQUISITION`; issue recorded | `PARTIAL`, 1 | PASS |
| Directory enumeration/resource failure | `ENUMERATION_ERROR` or limit issue | `PARTIAL`, 1 | PASS |

An actual kernel `read(2)` I/O failure could not be injected deterministically without a fault filesystem. Permission-denied required-file and directory cases were exercised. The `READ_ERROR` exception path was statically checked: it records an issue; the required result/digest gate also prevents `COMPLETE`. Deterministic injected read I/O failure is **NOT RUN**.

## 5. MEDIUM finding and statx review

- **Syscall use:** `ctypes.CDLL(None, use_errno=True)` obtains libc `statx`; the call uses `(fd, "", AT_EMPTY_PATH, STATX_MNT_ID, buffer)`. Return failure raises `Block`; missing symbol, absent returned mask bit, or per-entry failure does not fall back to `st_dev`.
- **ABI:** On this x86_64 Linux 6.12 environment, a 256-byte buffer, 32-bit mask at offset 0, and 64-bit `stx_mnt_id` at offset 144 match the UAPI structure. The independent direct syscall probe passed. Other architectures and libc/kernel combinations were not tested here.
- **Descriptor identity:** The collector queries mount ID using the already-open descriptor used for subsequent enumeration/read, after `fstat` identity comparison. `st_dev` remains recorded metadata but is not used as the mount boundary predicate. This correctly distinguishes same-device mounts when their mount IDs differ.
- **Error/unavailable behavior:** preflight lack of descriptor mount-ID support aborts before output creation with exit 2; per-object error or unavailable mask produces `MOUNT_ID_UNAVAILABLE`, `BLOCKED`, and exit 2. Simulated differing ID produces `MOUNT_BOUNDARY`, `PARTIAL`, exit 1, and stops descent.
- **Bind mounts and root policy:** No real mount was made. The mismatch branch passed only through a synthetic test hook; bind-mount protection is therefore dynamically **NOT RUN**. Each configured root establishes a fresh accepted mount ID. A mount already present exactly at a configured root is accepted, and ancestors traversed to reach that root are not compared to a separately pinned expected ID. This is consistent with the stated per-root model but does not authenticate intended mount topology. A malicious/pre-existing mount at a configured root is a residual environmental/policy risk.
- **Bypass/race analysis:** Once a directory descriptor is open, traversal remains anchored to it; a mount ID is checked before descending/reading. This narrows path replacement races and catches descendant mount changes. It does not make the entire walk an atomic snapshot, prevent a privileged concurrent mount actor from changing later lookups, or establish host/snapshot provenance. No mount-ID value or mask validity is persisted in the output, so the record does not independently attest the observed boundary decisions.

## 6. LOW finding verification

The collector's `CONTENT_NAMES` is an exact basename set. The raw-copy predicate accepts fixed named leaves and a direct child of the named backup directory only when its basename is in that set. Unexpected `.absent`/`.existed` lookalikes are marked `sha256_only_unapproved_marker`, generate `UNEXPECTED_BACKUP_MARKER`, and are not copied. The adversarial suite checks both `private.absent` and a dot-prefixed lookalike and compares their bytes against all captured files. Final/intermediate symlink paths are not followed; the outside sentinel payload did not appear in output. Tests also verified output path traversal refusal and that an existing destination sentinel remained intact.

One policy nuance remains documented: non-allowlisted regular payloads directly under the named transaction directory are still hashed (read) under the M34.1 contract; only exact approved names are copied. This follows the stated acquisition contract and is not an unexpected raw-copy path.

## 7. Test matrix and statuses

| Validation | PASS | FAIL | SKIP | NOT RUN | Notes |
|---|---:|---:|---:|---:|---|
| M34.1 suite | 26 | 0 | 0 | 0 | Synthetic regression suite |
| M34.2 suite | 17 | 1 | 3 | 0 | One intentional historical false-COMPLETE expectation; truncation, output I/O injection, real bind mount skipped |
| M34.3 suite | 11 | 0 | 0 | 0 | Synthetic M34.3 regression suite |
| M34.4 independent suite | 15 | 0 | 0 | 0 | Type/confusion, required evidence, race, statx branches, allowlist, checksums, hostile output, bounds, interruption |
| `bash -n` collector | 1 | 0 | 0 | 0 | PASS |
| Embedded Python `compile()` | 1 | 0 | 0 | 0 | PASS, no bytecode file emitted |
| ShellCheck | 0 | 0 | 1 | 0 | Executable unavailable |
| `statx` direct ordinary-path probe | 1 | 0 | 0 | 0 | Local kernel only |
| Real bind-mount creation/traversal | 0 | 0 | 0 | 1 | Prohibited; no real mount protection claim |
| Privileged/VM acquisition and qualification | 0 | 0 | 0 | 1 | Not authorized; no Alpha M1 qualification claim |

The original M34.2 FAIL is preserved, not treated as a product regression: its assertion encodes the vulnerability M34.3 intended to remove. Independent assertions inspect the resulting manifest, issue reason, global classification, and exit code. No CTest suite was applicable to these Python/bash collector changes.

## 8. Security review findings

### Confirmed findings

No newly introduced confirmed vulnerability was found in the local synthetic paths exercised. M34.2 HIGH and LOW remediations were confirmed by independent manifest and output-content checks. M34.2 MEDIUM's syscall logic was inspected and the supported local branch exercised, but actual bind-mount enforcement remains unverified.

### Residual risks and limitations

1. **Initial mount identity is accepted per configured root — environmental/policy risk, medium.** Prerequisite: an unintended mount is already attached at a configured source-root path, or the reviewed guest's mount topology differs from the intended contract. Impact: collection may traverse an unintended subtree while treating that mount as the allowed root boundary. Evidence: static policy at root traversal; M34.3 report documents the same assumption. Reproducibility: not attempted; mount operations were excluded. Mitigation: independently inspect and approve guest mount topology/root IDs before any future acquisition, or pin expected mount identity through a separately reviewed policy.
2. **Mount decisions are not recorded in the manifest — auditability limitation, low.** The code uses descriptor mount IDs to decide traversal but records neither per-root nor per-entry IDs. This does not bypass the current equality check; it makes an output reviewer unable to reconstruct the observed mount boundary from the manifest. Reproducibility: static. Mitigation: consider recording observed IDs and root-to-descendant comparisons in a future reviewed collector revision.
3. **Live acquisition remains non-atomic — residual forensic risk, medium.** Prerequisite: concurrent writer or topology change with sufficient access during acquisition. Metadata and inode checks detect common mutation/replacement and force a partial result, but cannot create a point-in-time snapshot. In some changed-file paths the bytes read can still be represented as captured output alongside a change issue; consumers must reject/segregate such `PARTIAL` evidence and never interpret it as a coherent snapshot. Mitigation: preserve the partial output and use an approved immutable snapshot if future policy allows.
4. **Unverified platform scope — environmental limit.** The ABI was checked dynamically only on Linux 6.12 x86_64. Debian 13 guest operation, other architectures, unsupported libc/kernel behavior, actual filesystem faults, and privilege-specific access behavior were not exercised.

No evidence supports a claim that same-device bind-mount protection has been verified dynamically. The synthetic mismatch hook proves classification behavior only.

## 9. Verdict and conditions for operator review

**`GO_FOR_OPERATOR_REVIEW`** is supported for reviewing the exact M34.3 collector and its assumptions. Before any distinct operator decision, review the exact collector digest and provenance, confirm the approved guest/snapshot/boot identity, independently inspect mount topology at every configured root, and accept that `COMPLETE` only describes this collector's implemented contract. If mount identity cannot be established or the expected root mount is unclear, remain blocked. This report grants no privileged acquisition authority.

**Alpha M1 remains `NO-GO / NOT QUALIFIED`.** No VM evidence or privileged qualification campaign was performed.
