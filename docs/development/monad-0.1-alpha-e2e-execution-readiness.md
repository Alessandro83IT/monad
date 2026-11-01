# Monad 0.1.0-alpha.1 E2E execution readiness

Review date: 2026-10-31  
Repository HEAD reviewed: `668b5515d4e6ae72eb4074e14b3b558d8d3d148f`  
Harness: `scripts/monad_alpha_e2e.py`  
Plan reviewed: `monad-0.1-alpha-privileged-release-test-plan.md`

## Verdict

**`BLOCKED_BY_ENVIRONMENT`**. The harness is a bounded functional test driver, but Athena-Alpha-M1 is the existing evidence-bearing VM, not a disposable guest-wide boundary. The harness and its path checks cannot constrain writes by a process holding initial-user-namespace `CAP_SYS_ADMIN` or by package build commands. This review did not execute the privileged test, alter the VM, or touch historical evidence. Execution on Athena-Alpha-M1 is not recommended until the operator establishes and reviews an adequate guest-wide isolation boundary that preserves its existing state and evidence.

## Harness review

Authorization is an exact required token and is checked before path, binary, capability, or filesystem work. The harness checks effective `CAP_SYS_ADMIN` through `capget` and compares `/proc/self/ns/user` with the initial namespace inode. It requires an executable binary reporting exactly `Monad Package Manager 0.1.0-alpha.1`, a clean PATH, and absence of loader/Python/shell override variables. Commands use argv arrays (no shell), a small environment, isolated cwd/TMPDIR, closed stdin, captured combined output, and a 900-second timeout.

Area validation rejects `..`, symlink components, non-`/var/tmp` locations, non-caller-owned or group/world-accessible directories, and nonempty directories. The Monad root is created below the area. Runtime assertions check activation payload/link, store payload, generations, info, switch, rollback, and removal. The script has no cleanup operation; it intentionally leaves fixture, root, and result evidence in place. Failures within the test sequence are recorded and stop execution. Setup/preflight failures before the main operation `try` block produce stderr failure JSON but no `results.json`; preserve terminal output as evidence in that case.

The in-process containment checks are useful against accidental path mistakes, but not a security boundary. In particular, capability-bearing `mpkg` invokes CMake/build logic for the package. A compromised or unexpectedly behaving binary/build process can write anywhere visible in the VM, including historical evidence. Prefix checks and a temporary directory do not change that.

## Filesystem write scope

Intended harness writes are limited to the caller-provided empty area beneath `/var/tmp`:

- `case/root/`: Monad `--root`, including repositories/state/package manifest, stores, generations, target activation, caches and other CLI state.
- `case/fixture/`: generated source, CMake file, and gzip archive.
- `results.json`: operation argv/output/status and overall result.
- `case/` is the subprocess cwd and TMPDIR; HOME is also set there.

The harness itself does not remove files. Python may create runtime caches only if the invocation environment permits them; the proposed `env -i` invocation omits Python path overrides. Subprocess writes are not reliably enumerable from source because the binary and build backend execute with the same filesystem visibility and privilege. Thus intended paths do not prove actual confinement. The harness checks the activation path resolves below `root/store`; its store walk does not follow symlink directories and rejects device changes.

No intended operation names `/`, `/usr`, `/etc`, `/var/lib`, the repository, or qualification/evidence paths as a target. This is not a guarantee those paths cannot be modified by a faulty/hostile privileged child. The plan's claim that a disposable VM protects the test is valid only if the VM is genuinely disposable and isolated. It does not describe Athena-Alpha-M1's existing historical evidence and therefore cannot establish that evidence is protected there.

## Subprocess inventory

The sole subprocess site is `subprocess.run([binary, "--root", root, ...])`, with shell disabled by default. The labels and CLI operations are:

1. `--version`
2. `install monad-alpha-fixture`
3. `generation current`
4. `info monad-alpha-fixture`
5. `generation list`
6. `generation switch <initial-id>`
7. `generation switch <installed-id>`
8. `generation rollback`
9. `generation switch <installed-id>`
10. `remove monad-alpha-fixture`
11. `generation current`
12. `generation list`

The CMake backend/build process is launched by `mpkg`, so its exact descendant processes are not enumerated or constrained by Python. No sudo, mount, capability mutation, or cleanup subprocess exists.

## Prerequisites and proposed command

Prerequisites: reviewed matching `mpkg` alpha.1 executable; Python 3; CMake 3.20 or later available through `/usr/bin:/bin`; Debian 13; `/proc`; local xattr-capable filesystem suitable for Monad's scanner and mount identity checks; initial user namespace with effective `CAP_SYS_ADMIN`; and a caller-owned empty mode-0700 directory under `/var/tmp`. The repository and binary provenance/version/hash must be recorded. Capability is broad; a nested user namespace does not satisfy the check.

For an environment that has independently passed isolation review, the command proposed by the plan is:

```sh
env -i PATH=/usr/bin:/bin \
  /usr/bin/python3 scripts/monad_alpha_e2e.py \
    --authorize RUN-IN-APPROVED-DISPOSABLE-VM \
    --mpkg /opt/monad-review/mpkg \
    --area /var/tmp/monad-alpha-run-<unique>
```

Replace `<unique>` with a new unique component and use absolute reviewed paths. The plan's generic invocation must not be treated as authorization to execute on Athena-Alpha-M1. No exact Athena-Alpha-M1 execution command is recommended under current evidence-preservation constraints.

## Evidence, failure handling, and recovery

Expected successful evidence: preserved `results.json`; terminal output; source revision and binary SHA-256/version; Debian/kernel/VM identity; namespace/capability preflight; filesystem type/options; zero exit for every listed CLI operation; installed symlink and payload/store content checks; generation transitions; info output; and removal/target absence. A failure stops the sequence and leaves files in place; preflight/setup failures may have no JSON receipt. Do not rerun in the same area. Preserve all evidence and use a separately approved recovery process. There is no harness rollback/cleanup. The plan's VM destruction procedure cannot be applied to Alpha M1 without defeating preservation of historical evidence.

The harness does not execute privileged CTest cases and does not perform full store manifest re-verification. The scanner was not weakened. The plan's recorded historical CTest totals are not results of this review.

## Corrections and verification

Added `tests/monad_alpha_e2e_guard_test.py` for nonprivileged checks that a wrong authorization token, parent traversal, and symlink component are rejected before capability preflight or test-area creation. These tests do not exercise the privileged install flow. The harness currently has no discovered production source correction to make; the blocking issue is the mismatch between its disposable-VM threat model and the selected evidence-bearing VM. Test execution status is to be recorded after running the targeted test.

Remaining risks include broad capability exposure, arbitrary descendant build commands, dependence on exact CLI output/format, lack of crash-resistant result journaling during every setup step, and the fact that test success is not release qualification. No VM, snapshot, historical evidence, system path, or installed file was changed.
