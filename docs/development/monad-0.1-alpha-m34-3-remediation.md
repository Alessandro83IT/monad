# Monad 0.1 Alpha M34.3 — security remediation and regression qualification

**Verdict: `GO_FOR_INDEPENDENT_REVIEW`. Alpha M1 remains `NO-GO / NOT QUALIFIED`. Privileged acquisition is not authorized.** This session used synthetic temporary fixtures only. No real acquisition, privileged test, mount operation, installation, rollback, canary, or administrative path access was performed.

## 1. Executive summary

All three M34.2 findings have targeted remediations in the M34.1 collector. Required paths now have explicit file/directory type checks and required files need captured bytes and a digest before the collector can report `COMPLETE`. Recursive traversal reads Linux mount IDs from already-open file descriptors with `statx(STATX_MNT_ID)` and rejects descendant mount changes; inability to obtain mount identity blocks collection. Raw backup-marker copying now uses only the exact names in the existing `CONTENT_NAMES` contract; suffix lookalikes are reported and remain hash-only.

The original M34.1 suite passes 26 tests and the new M34.3 suite passes 11 tests. M34.2 remains unchanged. Its one test that asserts the old false-`COMPLETE` behavior now fails as expected; the three environment/race skips remain. This intentional failure documents the behavior change and is not hidden as a pass. Independent review is still required before any future operator review or privileged acquisition decision.

## 2. Baseline and initial SHA-256

Baseline was recorded before edits. Branch: `main`. HEAD: `abd6bac4b368427b76f6c98522afde928b418939`. The worktree had many pre-existing modified files, untracked artifacts, and build directories. None were changed or removed. The two permitted new paths were absent at baseline.

| File | Initial SHA-256 | Baseline result |
|---|---|---|
| `qualification/m34-1-targeted-forensic-acquisition.sh` | `053b4375cae4342e160b289fc9781a66b6a64dafae67be15dea29ff95314ffc9` | Matches required pin |
| `tests/m34_1_targeted_forensic_acquisition_test.py` | `c52bc8c5b8b2f947f081805de62e6455e0cbfa862ce684be54cf0ad0ea033d50` | Measured |
| `tests/m34_2_security_audit_test.py` | `fbed6b9c38f456d166212b031f29b98259fff1746a411df7af3fd16d82fe0d79` | Measured; not modified |
| `docs/development/monad-0.1-alpha-m34-1-acquisition-review.md` | `934ec1fcfa1344450e7308b10784e1c78253b124ef9edb81b07a7da17ca9a10a` | Measured; not modified |
| `docs/development/monad-0.1-alpha-m34-2-security-audit.md` | `a159cb3ac08e14aa2c2e1c6a9a32b4ebbdaf526c2c536321cfce7f33169abe21` | Measured; not modified |

The collector pin matched exactly, so scoped edits proceeded. No baseline or security pin was regenerated.

## 3. Finding reproduction

### HIGH — required file replaced by a directory

- **Synthetic input:** otherwise-valid private source fixture; the runner file was removed and replaced with an empty directory. All other required files and the M32.1 synthetic checksum were valid.
- **Before remediation:** M34.2 documented and reproduced exit code `0`, runner row `PRESENT`, and manifest `completion: COMPLETE`. This was the false completion defect.
- **After remediation:** runner row is `TYPE_MISMATCH`, `expected_type: file`; issue `REQUIRED_TYPE_MISMATCH` is recorded; completion is `PARTIAL`, exit code `1`.
- **Expected classification:** `PARTIAL` because collection completed but a required object has an incompatible type. The required backup transaction directory replaced by a regular file is likewise `PARTIAL`.

### MEDIUM — same-device bind mount boundary

The former check compared only `st_dev`, which does not distinguish a same-device bind mount. A real mount was not created. The M34.3 fixture hook simulates a changed mount ID for one already-open descriptor: the row becomes `MOUNT_BOUNDARY`, traversal stops at that object, completion is `PARTIAL`, and exit code is `1`. A missing or failed mount ID is a verification failure and produces `BLOCKED` (exit code `2`); unavailable support at preflight leaves no output directory, while an error discovered during traversal is recorded in the manifest.

### LOW — backup marker suffix overmatch

Before remediation, the suffix predicate authorized copying any direct backup child ending in `.absent` or `.existed`. The documented marker names were already present in the `CONTENT_NAMES` set. The suffix wildcard was removed. An exact approved marker such as `runner.absent` remains eligible for capture; an unapproved `private.absent` is given `sha256_only_unapproved_marker`, issue `UNEXPECTED_BACKUP_MARKER`, and `PARTIAL`, with no payload copied.

## 4. Changes and technical rationale

- `qualification/m34-1-targeted-forensic-acquisition.sh`
  - Added expected-type validation for all required objects: the named backup transaction is a directory; runner, support, sudoers, M32.1 inventory and checksum are regular files.
  - A required regular file must have its explicit captured-content status and SHA-256. Mismatches and missing required content prevent `COMPLETE` with verifiable issue reasons.
  - Added descriptor-bound mount identity using Linux `statx` with `AT_EMPTY_PATH` and `STATX_MNT_ID`. Each configured root directory establishes its own allowed mount ID; descendants must match. This follows the existing per-root boundary model and catches bind mounts beneath a configured root even when `st_dev` is unchanged.
  - If `statx` or `STATX_MNT_ID` is unavailable, or descriptor verification fails, the collector fails closed. Per-object uncertainty is `BLOCKED`; an identified boundary crossing is `PARTIAL` and no descent occurs.
  - Removed `.absent`/`.existed` suffix wildcard from raw-copy authorization. Unknown suffix markers are explicitly reported and remain hash-only under the existing transaction payload hashing rule.
- `tests/m34_1_targeted_forensic_acquisition_test.py`
  - Added effective-result checks for required file/directory type substitutions, unavailable mount IDs, and unapproved markers.
- `tests/m34_3_security_regression_test.py` (new)
  - Added fixture-only regression cases for the HIGH, MEDIUM, and LOW findings, symlinks, absent/inaccessible paths, mutation while hashing, output collision, result classification, output checksum integrity, and unchanged source fixtures.
- `tests/m34_2_security_audit_test.py` remains byte-for-byte unchanged as the independent historical audit suite.

### Linux mount primitive choice

`statx` mount ID is obtained from the opened descriptor, not by resolving a pathname after the fact. This avoids a path-string-only check and narrows the check/use race: the descriptor used for traversal is the descriptor whose mount ID is checked. `openat2(RESOLVE_NO_XDEV)` could enforce a no-crossing policy during resolution, but the collector is Python-only after its shell launcher and uses descriptor-relative calls; adopting `openat2` would require a syscall wrapper and kernel ABI handling. `mountinfo` parsing is path/topology based and would require reconciling escaped paths and concurrent changes. `st_dev` alone is insufficient. The chosen check is compatible with the documented model provided that each configured root's initial mount is accepted as the root boundary.

This is not a claim that the collector independently authenticates the intended mount for a configured root. A mount already present at the configured root is accepted as that root's starting boundary. Host-side mount topology and snapshot identity still require independent evidence. Mount IDs are kernel observations, not a provenance attestation.

## 5. Resulting security contract

- `COMPLETE` requires all configured top-level roots to be inventoried without issues, every required path to be present with its declared type, all required regular-file bytes captured with a digest, and the M32.1 inventory/checksum relationship to validate.
- `PARTIAL` means collection ran but found missing/incompatible evidence, a known mount boundary, source anomaly, traversal/resource issue, or checksum inconsistency. The manifest carries a reason where output creation succeeded.
- `BLOCKED` means preflight could not establish required guarantees (including mount identity), mount verification failed during traversal, or collection was interrupted. Preflight refusal may have no manifest; an in-walk mount identity failure is represented in the manifest.
- Final and intermediate source symlinks remain unopened. Opens remain descriptor-relative and no-follow. Existing output destinations remain refused, files remain exclusive, and per-file/total/depth/entry/enumeration/output limits remain unchanged.
- Exact marker names can be copied. Non-allowlisted transaction payloads can still be hashed only under the prior M34.1 rule; suffix lookalikes are never copied. Hashing reads source bytes but does not write source content.
- SHA256SUMS binds the produced inventory and captured payloads against later accidental changes; it does not authenticate collector, source, operator, timestamp, or transfer chain.

## 6. Test matrix and results

| Validation | Result | Detail |
|---|---|---|
| Original M34.1 synthetic suite after remediation | **PASS** | 26 tests; includes file-to-directory and directory-to-file cases |
| New M34.3 synthetic regression suite | **PASS** | 11 tests |
| M34.2 independent audit suite | **FAIL (intentional behavior change)** | 21 tests: 17 pass, 1 old false-COMPLETE expectation fails, 3 skip |
| M34.2 false-COMPLETE assertion | **FAIL as expected** | Collector returned exit 1 and `PARTIAL`, conflicting with the test's old expectation of exit 0/`COMPLETE` |
| M34.2 output I/O fault injection | **SKIP** | No dedicated fault filesystem or reliable ENOSPC/EIO injection |
| M34.2 real same-device bind mount | **SKIP** | Mount operations prohibited; synthetic mount-ID hook used in M34.3 |
| M34.2 truncation during read | **SKIP** | No deterministic hook in unchanged suite; timing race is unreliable |
| `bash -n` | **PASS** | Collector parses |
| Embedded Python compilation | **PASS** | `compile()` used by M34.1 and M34.3 suites; no bytecode artifact required |
| ShellCheck | **SKIP** | `shellcheck` executable is not installed |
| Relevant static scope review | **PASS** | Fixed roots/allowlists retained; no new administrative path, subprocess, network, mount, or privilege mechanism |
| Real privileged/VM acquisition checks | **NOT RUN** | Explicitly outside authorization and prohibited by session constraints |
| Real bind mount kernel behavior | **NOT RUN** | Synthetic descriptor-ID simulation only; no mount operation performed |

The M34.2 FAIL is preserved and reported, not edited away. Its assertion still demonstrates the original vulnerability but can no longer pass against the corrected collector. The other M34.2 skip reasons are unchanged.

## 7. Adversarial review

Reviewed source traversal, descriptor lifecycle, mount checks, type checks, content authorization, hashing, output creation, and classification after the changes.

- Source traversal still uses `O_NOFOLLOW`, descriptor-relative opens, and inode/type comparison after opening. No new path-based mount check or symlink-following operation was added.
- Mount ID is obtained on the exact opened object. A known differing ID stops descent; a missing/error ID blocks. It cannot prevent a privileged actor from altering mount topology after the observation, and it does not authenticate the initial mount accepted for a configured root.
- Required file mismatch is handled before completion is computed. Required data that lacks capture/digest is also an issue. Enumeration/open/stat errors already add an issue and therefore cannot yield `COMPLETE`.
- Unknown markers retain only the existing hash-only backup policy; raw bytes are captured only for exact `CONTENT_NAMES` entries. The test compares captured output with the hostile payload and verifies it is absent.
- Hash-before/after metadata and inode checks remain. A synthetic concurrent mutation is detected and the produced `SHA256SUMS` is verified against every output file. A privileged writer able to restore metadata or change the kernel remains outside the model.
- Existing outputs are not overwritten. Tests compare fixture source file digests before and after collection; no source writes were observed.
- Resource bounds and fixed acquisition roots are unchanged. Captured bytes and traversal names remain bounded by existing file, total, entry, enumeration and inventory limits.

No implementation regression was found in the tested paths. Output can remain incomplete after abrupt process or storage failure, as already documented; consumers must reject output without a valid manifest and checksum set.

## 8. Compatibility with M34.1

Manifest schema remains version 1. Existing fields are retained; affected result rows gain `expected_type` only on a type mismatch and may use the new statuses `TYPE_MISMATCH`, `REQUIRED_CONTENT_UNAVAILABLE`, `MOUNT_BOUNDARY`, and `MOUNT_ID_UNAVAILABLE`. `COMPLETE` is intentionally stricter. Existing valid synthetic acquisitions retain exit code 0; partial acquisitions retain exit code 1; blocked conditions use exit code 2. Kernels/libc environments without descriptor-based `STATX_MNT_ID` now fail closed and may not create output.

The exact collector pin has changed due to reviewed source edits. No installer pin, baseline, runner, installer, or administrative artifact was changed or regenerated. Any downstream pin consumer must be updated only through the separate reviewed promotion workflow.

## 9. Residual risks and sandbox limits

- The initial mount attached to each configured root is treated as allowed. This does not detect an unintended mount that is already mounted exactly at a configured root; independently review mount topology before any future acquisition.
- Mount IDs are checked at open time, not atomically enforced for the lifetime of a walk. A privileged concurrent mount actor or compromised kernel can invalidate the observation.
- No actual bind mount, privileged permissions, VM identity, host snapshot, ACL/xattr, storage fault, or power-loss behavior was exercised.
- `statx` ABI behavior was exercised on this Linux 6.12 environment through actual synthetic descriptor reads; missing/error behavior was simulated. Other supported kernel/libc combinations need review.
- A preflight `BLOCKED` can leave no inventory. A process crash or SIGKILL can leave an output directory incomplete. Preserve and independently inspect any such directory; never infer completeness from its existence.
- The collector remains unsigned; SHA-256 does not establish provenance. No real Alpha M1 acquisition or qualification claim follows from these tests.

## 10. Final SHA-256

| File | Final SHA-256 |
|---|---|
| `qualification/m34-1-targeted-forensic-acquisition.sh` | `56771879aa73bbdcac4368c9babc990f73150401b6aecfc8cb2646263c34d9d9` |
| `tests/m34_1_targeted_forensic_acquisition_test.py` | `938254e4cd8cf53df0e1b4051c7a8febb6a70ac189733196f6d2351b9d94f4f8` |
| `tests/m34_3_security_regression_test.py` | `ea8183a31ead5e1ec20980d7d74d3eeb4ecd5dddf117a73a21f9dd6a69654fcb` |

The historical M34.2 test and both M34.1/M34.2 review documents remain unchanged from the initial hashes above.

## 11. Independent assessment and verdict

The M34.2 findings have targeted code fixes and synthetic regression coverage, but the result is suitable only for another independent code/security review. Review should verify the exact final collector digest, confirm the Linux `statx` ABI and mount-boundary policy on the intended Debian 13 guest, review the exact marker contract, and independently inspect generated synthetic manifests. No operator acquisition is authorized by this report.

**Verdict: `GO_FOR_INDEPENDENT_REVIEW`. Alpha M1 remains `NO-GO / NOT QUALIFIED`.** No privileged acquisition, qualification campaign, release, or pin promotion is authorized.
