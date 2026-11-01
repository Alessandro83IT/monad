# Monad 0.1.0-alpha.1 local E2E qualifier

Date: 2026-10-31
Launcher: `scripts/monad_alpha_qualify.py`
Harness: `scripts/monad_alpha_e2e.py`

## Commands

The launcher is an ordinary-user client and must never run through `sudo`.
Its only privileged interface is the fixed operation API at
`/usr/libexec/monad-alpha-qualify-runner`. Until an administrator has
separately installed and reviewed that protected runner and its root-owned
policy, both modes fail closed because the runner is absent.

After reviewed setup, run a preflight and allocate a new private evidence
directory under `/var/tmp`:

```sh
env -i PATH=/usr/bin:/bin \
  /usr/bin/python3 scripts/monad_alpha_qualify.py --check
```

`--check` never launches the E2E harness. If all gates pass, it creates an
exclusive caller-owned `0700` directory and writes `preflight.json` and its
SHA-256 record with mode `0600`. It never reuses or cleans a prior directory.
If preflight gates fail, it exits nonzero and reports all observed blockers.

The run form still requires explicit user authorization:

```sh
env -i PATH=/usr/bin:/bin \
  /usr/bin/python3 scripts/monad_alpha_qualify.py --run \
    --authorize-e2e AUTHORIZE-MONAD-ALPHA-1-E2E-ON-CLONE
```

The launcher calls only the fixed runner with `--operation {preflight,run}`
and a validated fresh area. It never invokes the harness itself and never
accepts an original UUID or clone-isolation assertion from the caller.

### Administrative preparation (procedure only; not performed here)

An administrator must review and install a dedicated runner from a
hash-pinned source into root-owned `/usr/libexec/monad-alpha-qualify-runner`
(root:root, mode 0755, with non-writable parent directories). Install the
matching harness and artifact hashes in a protected root-owned directory.
The runner accepts exactly `--operation {preflight,run} --area
/var/tmp/<single-component>` and rejects extra arguments, unsafe environment,
symlinks, non-empty areas, and paths outside `/var/tmp`. No shell, general
sudo rule, setcap, or generic execution API is permitted.

Its root-owned policy must hold the original UUID and approved clone identity
provisioned from an independent VirtualBox Manager review. The runner reads
the guest UUID as root, compares it with that policy, and independently binds
the selected clone UUID to clone-owned writable disks, disconnected network
adapters, and no shared folders. It also repeats guest mount/interface checks
immediately before launch. Caller UUIDs, confirmation strings, environment
variables, or receipts are never identity evidence. If independent host-side
clone metadata cannot be verified, `run` must be refused.

The runner performs checks and launch atomically. It validates artifacts by
descriptor, copies them into a unique protected run directory, rehashes the
copies, and executes only those copies plus the protected harness. It never
runs workspace Python as root or grants capabilities to Python with setcap.
This administrative component is a prerequisite and was not installed here.

To use an already provisioned fresh area, add `--area
/var/tmp/monad-alpha-e2e-<unique>` to either mode. The path must be a new,
empty, real directory owned by the caller, mode `0700`, and a direct child of
`/var/tmp`. A pre-existing directory is never emptied.

## Fail-closed gates

- Debian 13 and VirtualBox guest detection, a nonempty DMI product name, and
  a well-formed guest UUID. DMI product strings do not establish the
  VirtualBox host-side VM name; host review and clone-isolation confirmation
  remain required before `--run`.
- No VirtualBox shared-folder mounts, IPv4/IPv6 default route, or connected
  non-loopback interface. The launcher only observes network state.
- Ordinary unprivileged client with exact clean `PATH` and no loader/Python/
  shell environment overrides. The protected runner checks the initial user
  namespace and effective `CAP_SYS_ADMIN` for the exact harness process.
- `/var/tmp` on a local ext4 mount that is read-write; ext4 provides xattrs,
  and filesystem identity/options are recorded.
- Alpha.1 executable version and SHA-256 for `stage/bin/mpkg` and
  `stage/lib/libathena_core.so`, checked against the hashes in the existing
  release preparation record. Ownership, regular-file/link count, symlinks,
  and group/world write permissions are checked. Any disagreement is a hard
  stop; no pin or attestation is regenerated.
- Fresh caller-owned private evidence area; harness itself independently
  checks its authorization token, capability, namespace, paths, and area.

The guest can identify VirtualBox but cannot establish its registered VM name,
UUID relationship to the original, writable disk ownership, or full host mount
configuration. The operator must use VirtualBox management evidence outside
this launcher. DMI fields and the guest-visible UUID alone do not prove that
clone storage is independent. The guest-wide disposable boundary remains
required because the harness runs package build logic with broad capability.

## Automatic tests

```sh
python3 tests/monad_alpha_qualify_test.py
python3 tests/monad_alpha_e2e_guard_test.py
```

Both are nonprivileged and do not run the harness. CTest registers these as
`monad_alpha_qualify_test` and the three `monad_alpha_e2e_guard_*` checks.

## Local implementation result

On the observed guest, Debian 13 and VirtualBox are visible, and DMI reports
generic product name `VirtualBox`; this is a firmware label, not a VM-name
mismatch. The staged
`mpkg` SHA-256 is `6ebd8413d03396591c32ad6d82152fde42ca4ed3a18700d6f41adceb3dc55ed9`,
while the existing release record says
`c9b06dcb9621e50175afab4cefc090313aab9b36665047b48f20a76b717c926f`. The
staged core library hash matches the recorded value
`2d23dfcabc06a965a068f716be9fda26b465226f675c23eab4328061271ca2be`, but the
staging directories are mode `0775`. `/var/tmp` is on `/dev/sda2` ext4 mounted
read-only. Procfs contains an IPv6 `::/0` reject route on `lo`, while
`enp0s3` has carrier 1 and is up. The reject route is not an external usable
route; the connected interface is still an isolation blocker. These are
blockers, not reasons to rewrite the release hash or weaken the check. No E2E
was run and no directory was created under `/var/tmp` in this implementation
session.

The existing release preparation record reports 143 nonprivileged PASS, 0
FAIL, 4 SKIP and one excluded test; those are prior results. This task's new
launcher tests must be run separately and do not change the status of the 25
privileged tests or qualify the release.

Earlier verification recorded for this launcher:

- `python3 tests/monad_alpha_qualify_test.py`: **9 PASS**.
- `python3 tests/monad_alpha_e2e_guard_test.py`: **3 PASS**.
- Isolated CTest selection (`qualification_driver_test`, launcher test, and
  three harness-guard tests): **5 PASS, 0 FAIL**.
- `env -i PATH=/usr/bin:/bin python3 scripts/monad_alpha_qualify.py --check`:
  **BLOCKED** (exit 2). Observed blockers: DMI name is generic `VirtualBox`;
  IPv6 has a default route and `enp0s3` reports carrier 1; effective
  `CAP_SYS_ADMIN` is absent; `/var/tmp` is on a read-only ext4 mount; and the
  staged binary's SHA-256 differs from its recorded value. The stage
  directory is also group writable. No evidence area was created. The
  `--check` path did not invoke the E2E harness.
- Privileged E2E and privileged CTest cases: **NOT RUN**, as required.

Current audit verification:

- Launcher unit tests: **13 PASS**; E2E guard tests: **3 PASS**.
- Isolated CTest selection: **4 PASS, 0 FAIL, 0 SKIP, 0 TIMEOUT**. The
  `qualification_driver_test` is outside this selection and was **NOT RUN**.
- Read-only inspection confirms DMI `VirtualBox`, `CAP_SYS_ADMIN` absent,
  UID mapping `1000 0 1`, `/var/tmp` read-only, stage directories mode `0775`,
  staged `mpkg` hash `6ebd8413d03396591c32ad6d82152fde42ca4ed3a18700d6f41adceb3dc55ed9`,
  and core-library hash matching its pin. DMI product UUID access is denied,
  an additional identity blocker. The procfs `::/0` row has reject flags and
  is excluded by the corrected route check. No E2E or real `--check` was run.

## Technical audit follow-up

The DMI name comparison was removed because firmware product name is not the
VirtualBox registry name. The preflight records the DMI string and explicitly
records that host VM-name verification has not happened. IPv6 default-route
classification now requires the all-zero destination, prefix length `00`, an
up route flag, and no reject flag. This avoids treating a kernel reject route
on `lo` as an external route and avoids treating a more-specific prefix as a
default. Initial-user-namespace detection now checks the full identity UID
mapping instead of a fixed namespace inode, which is not stable across
systems. CAP_SYS_ADMIN remains mandatory.

The active `enp0s3` carrier, read-only `/var/tmp`, group-writable
staging directories, and `mpkg` hash mismatch remain genuine environmental or
artifact-integrity blockers. The recorded expected hash is unchanged. These
checks were not relaxed.
