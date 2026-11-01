# Monad 0.1.0-alpha.1 Privileged Release Test Plan

Preparation review: 2026-10-31  
Source HEAD reviewed: `6daa4a0b9a67441e7eacfc2ac9a3a4ae8d4acbe1`  
Target: `0.1.0-alpha.1`

## 1. Executive summary

The normal install path cannot be demonstrated in the current unprivileged
host environment. The shared artifact scanner returns success for absent xattrs
only if the filesystem explicitly reports xattrs unsupported, or if the
process has effective `CAP_SYS_ADMIN` in the **initial user namespace**. It
currently has neither. The scanner must remain fail closed.

The proposed environment is a newly provisioned, disposable Debian VM separate
from Alpha M1, with a dedicated local test directory and a process granted
only the scanner prerequisite. No VM was created, started, or changed during
this preparation. No privileged test or harness execution occurred.

The reusable harness is `scripts/monad_alpha_e2e.py`. Its existence is not
functional evidence. Current verdict: **`TEST_PLAN_READY_FOR_AUTHORIZATION`**;
this is authorization to review the plan, not evidence that installation works
or that a release is ready. The previously reported 140 PASS, 4 SKIP, 0 FAIL
nonprivileged suite and 25 privileged cases NOT RUN remain historical results,
not results produced by this session.

## 2. Selected environment

Use a fresh Debian VM dedicated to Monad alpha testing, separate from Alpha M1
and containing no valuable state. Use a normal local filesystem for the
disposable area (ext4 is the known candidate); do not use a nested user
namespace as a substitute for initial-namespace capability. Keep the VM
offline except for provisioned build dependencies and transfer the reviewed
source/build artifact and fixture inputs by an approved route.

| Candidate | Assessment |
|---|---|
| Disposable Debian VM | Selected. Gives a clear guest-wide boundary and can run a process with the required initial-namespace capability without changing host privilege. VM provisioning/startup is outside this preparation and requires operator authorization. |
| Isolated filesystem alone | Not sufficient. It does not provide the initial namespace capability; a dedicated filesystem can reduce impact but does not satisfy scanner inspection. |
| Dedicated temporary root | Required as the Monad state root, but not a privilege boundary for package build commands. |
| Private mount namespace | Not sufficient: it does not imply initial-namespace `CAP_SYS_ADMIN`; mount namespace setup also requires privilege. Monad's package E2E path has no planned mount/umount operation. |
| User namespace | Explicitly rejected as a solution. Scanner checks initial namespace identity as well as the effective capability. |

## 3. Required capabilities and filesystem

**Confirmed by source:** `src/artifact/scanner.cpp` invokes `capget`, requires
effective `CAP_SYS_ADMIN`, and checks `/proc/self/ns/user` against the initial
user namespace inode. Linux hides `trusted.*` xattr names from an ordinary
caller, so an empty visible xattr list is not sufficient. The capability is
checked in staging, object-store, generation, and adoption scans through the
shared scanner. A nested user namespace capability does not meet the check.

**Root status:** UID 0 is not directly checked by the scanner. Therefore root
is not a code-level requirement if an operator can grant effective initial
namespace `CAP_SYS_ADMIN` to the test process. In practice, arranging that
capability is an administrative action. The proposed VM execution is an
operator-controlled process with that single required capability; do not use
unrestricted host root. Do not use `sudo` in the harness. Do not add file
capabilities to an unreviewed workspace binary.

**Filesystem:** all state must be on a filesystem where xattrs are supported
and can be completely listed by the test process. `EOPNOTSUPP` from xattr
listing is accepted by the scanner as explicit evidence of no xattrs, but the
plan does not rely on that route. Mount identity checks require related store,
generation, and target objects to remain on the expected mount. No mount
operation is needed for this package workflow. Confirm the chosen VM filesystem
type and mount options before the run; capability preflight alone cannot prove
filesystem suitability.

**Assumptions to verify at execution:** the VM image's kernel exposes the
scanner's initial namespace check as expected; `/proc` is mounted; the selected
filesystem supports xattrs; build tools and CMake required by the fixture are
available; and the reviewed `mpkg` binary is the build for the source under
review.

## 4. Isolation boundaries

Create a new, empty mode-0700 directory under `/var/tmp` in the disposable VM,
owned by the operator account that runs the harness. The harness creates its
fixture and Monad `--root` below this directory, and writes `results.json`
there. `--root` maps store, build/cache, state, repository state, generations,
and target beneath the supplied root. Fixture source/archive and package
manifest also remain beneath the area.

The disposable **guest** is the ultimate boundary: package installation runs
CMake and package-controlled build instructions. The harness uses a fixed,
minimal CMake fixture, a scrubbed child environment, a bounded working
directory and temporary directory, and a command timeout. It does not claim
that a Python path walk can contain a malicious binary or kernel-level writes.
Keep the guest disposable, avoid secrets and host shared folders, and do not
attach production filesystems. Review VM mount configuration before
authorization.

## 5. Fixture specification

- Package name/version: `monad-alpha-fixture` / `1`.
- Local gzip tar archive with exactly one top-level source directory.
- CMake project with `LANGUAGES NONE`; installs one known text file to
  `share/monad-alpha-fixture/proof.txt`.
- Manifest uses a local `file://` archive, its computed SHA-256, CMake backend,
  and no dependencies.
- Expected installed target entry is a Monad activation symlink whose content
  resolves to the exact expected payload in the store.
- No network source, shell hook, credentials, or external fixture is used.

## 6. Test harness

Location: [`scripts/monad_alpha_e2e.py`](../../scripts/monad_alpha_e2e.py).

It requires the exact `--authorize RUN-IN-APPROVED-DISPOSABLE-VM` token, an
executable `--mpkg`, and a pre-created empty mode-0700 caller-owned area under
`/var/tmp`. It refuses `/`, system directory targets, nonempty areas, unsafe
environment variables, missing effective `CAP_SYS_ADMIN`, and non-initial
user namespaces. Child commands receive an explicit small environment and run
with a 15-minute timeout. Each operation records argv, combined output, and
exit code in JSON; unexpected results record failure and stop. The harness
performs SHA-256 fixture verification and checks activation, store payload,
generation transitions, info, and removal.

Limitations: the harness is an orchestration and assertion tool, not a sandbox.
The VM boundary is mandatory. It does not run CTest privileged cases. It does
not invoke sudo, change capabilities, configure mounts, or perform cleanup.
After evidence review, the operator removes the disposable VM using the
operator's separately approved VM lifecycle procedure; harness cleanup is
intentionally absent so failure evidence is preserved.

## 7. Static verification results

No privileged harness execution, package installation, VM action, sudo, or
system modification was performed in this preparation. Safe static checks
performed on the new harness:

- Python AST parse: **PASS**.
- `python3 -m py_compile` directed to `/tmp` (no repository cache writes):
  **PASS**.
- Source review for subprocess argument arrays (no shell), fixed fixture
  source, explicit auth, path checks, scrubbed environment, timeout, JSON
  result, and absence of sudo/mount/cleanup commands: **PASS**.
- Functional behavior of this harness: **NOT RUN** by instruction.
- Existing CTest and nonprivileged suite in this session: **NOT RUN**; prior
  reports supply the historical 140 PASS / 4 SKIP / 0 FAIL result.

## 8. Exact proposed execution procedure

After the VM, binary, and procedure are explicitly approved:

1. Provision/start a fresh Debian VM separate from Alpha M1. Keep it disposable,
   offline where practical, and without host shared folders or sensitive data.
2. Transfer the exact reviewed Monad source/binary and required build tools.
   Record source revision, binary SHA-256, Debian release, kernel, and
   filesystem type/mount options. Confirm CLI reports `0.1.0-alpha.1`.
3. Create a new empty directory such as `/var/tmp/monad-alpha-run-<unique>`;
   set owner to the selected operator and mode 0700. Confirm the directory is
   on the intended local xattr-capable filesystem.
4. From the initial user namespace, arrange effective `CAP_SYS_ADMIN` for the
   harness process through the VM operator's approved mechanism. Do not grant
   capabilities to the binary itself. Confirm the process environment has no
   ambient loader overrides. The harness independently rejects a nested user
   namespace and missing capability.
5. Review and execute this exact command from the source checkout (substitute
   the real absolute paths):

   ```sh
   env -i PATH=/usr/bin:/bin \
     /usr/bin/python3 scripts/monad_alpha_e2e.py \
       --authorize RUN-IN-APPROVED-DISPOSABLE-VM \
       --mpkg /opt/monad-review/mpkg \
       --area /var/tmp/monad-alpha-run-<unique>
   ```

   The operator's capability mechanism must preserve the clean environment
   above; do not prefix this command with sudo unless a separately reviewed
   operator procedure explicitly chooses and documents that mechanism. The
   harness itself never requests privilege or escalates.
6. Preserve the full `results.json`, terminal output, binary hash, source
   revision, and VM identity for review. Do not rerun in-place after failure;
   preserve the area and use a new unique area for an authorized repeat.
7. Review all JSON operation exit codes and outputs, payload/store hash and
   contents, current/listed generation state, info output, post-remove target
   absence, and absence of unexpected paths. Then destroy the disposable VM
   through its separately authorized lifecycle process.

The script intentionally does not run `mpkg store verify`: no such CLI
subcommand is exposed. Store integrity in this minimal test is checked by
confirming the unique stored payload's bytes and the activated symlink's
resolved content; a stronger store verifier/API test should be added if the
release contract requires full manifest re-verification.

## 9. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Effective `CAP_SYS_ADMIN` is broad within the process namespace | Disposable isolated VM; no host directories or secrets; capability confined to guest, never host. |
| Build system executes package-controlled instructions | Fixed local fixture, no network, disposable guest, explicit operation review. |
| Harness or CLI writes outside designated Monad root | Guest-wide isolation is the actual boundary; no shared host paths; review VM mounts and preserve evidence. |
| Wrong binary/source tested | Record HEAD, build provenance, binary SHA-256, and version before execution. |
| Filesystem cannot satisfy xattr or mount assumptions | Confirm local filesystem and options; preserve fail-closed result if scanner rejects it. |
| Failure leaves transaction state | Stop immediately, preserve all area/logs, do not automatically clean or retry in place. |
| Harness static assertions do not match CLI output/state behavior | First authorized run is exploratory evidence; any mismatch is a FAIL requiring plan/harness revision, not a pass. |

## 10. Expected evidence

Successful run evidence must include: exact source revision and binary SHA-256;
VM/Debian/kernel identity; initial user namespace and effective capability
preflight; filesystem identity/options; `results.json` with zero exit for every
operation; install payload/link and store payload checks; successful info;
current/list consistency before and after transitions; switch to initial and
back; rollback; remove and target absence; and final evidence review showing
all writes stayed within the disposable guest/area. Preserve failed evidence
too. Any unexpected exit or boundary observation is a failure, never a silent
skip.

## 11. Remaining blockers

- End-to-end successful installation/removal has not been demonstrated.
- The four scanner-dependent nonprivileged CTest skips remain environment
  limited; 25 privileged tests are still NOT RUN and are outside this minimal
  alpha release test.
- The prior nonprivileged test record is not reproduced in this session.
- Full store manifest integrity has no CLI verifier in the tested path.
- No release archive, artifact manifest, checksums, or operator evidence review
  exists.
- Existing prior reports identified that the current host's scanner path
  cannot succeed unprivileged. This plan does not change that conclusion or
  weaken scanner policy.

## 12. Operator authorization requirements

Before execution, an operator must explicitly approve: provisioning and
starting a new disposable Debian VM separate from Alpha M1; guest capability
assignment to the exact process; the chosen filesystem and VM mounts; transfer
and execution of the reviewed binary; the unique disposable test area; and
preservation/review/destruction of the VM and results. This plan requests no
authorization for host sudo, VirtualBox operations, Alpha M1, snapshots,
forensic collection, system installation, publication, or privileged CTest.

## 13. Final verdict

**`TEST_PLAN_READY_FOR_AUTHORIZATION`**. The environment requirement is
identified and the harness is statically reviewable. No end-to-end functionality
claim is made. Successful install/remove evidence, evidence review, and release
artifact checks remain prerequisites to any release-readiness decision.
