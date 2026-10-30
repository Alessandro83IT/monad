# Monad 0.1 Alpha M28 — read-only administrative acquisition

## Scope and source basis

This procedure prepares a bounded read-only snapshot of administrative metadata. It does not establish authenticated provenance and does not qualify Alpha M1. The installer is the source of truth for installed destinations: [`install-athena-alpha-m1-admin.sh`](../../qualification/install-athena-alpha-m1-admin.sh), especially variable declarations at lines 8–19, transaction writer and manifest at lines 360–476 and 572–609, rollback status at 479–569, serialization at 713–739, and publication at 777–857. Runtime paths are cross-checked against [`athena-alpha-m1-runner.py`](../../qualification/athena-alpha-m1-runner.py), [`gateway_support.py`](../../qualification/gateway_support.py), and [`athena-alpha-m1-systemd-canary.py`](../../qualification/athena-alpha-m1-systemd-canary.py).

## Installer-derived administrative path inventory

| Path | Purpose and coverage |
|---|---|
| `/usr/local/sbin/athena-alpha-m1-runner` | Installed runner executable; metadata only, content not opened. |
| `/usr/local/sbin/gateway_support.py` | Installed support module. |
| `/etc/sudoers.d/athena-alpha-m1` | Installed policy; metadata only, content not opened. |
| `/var/log/athena-alpha-m1` | Complete recursive log directory inventory. |
| `/var/lib/athena-alpha-m1/ext4-fixture` | Fixture root, including all descendants; preserve D6 residue. |
| `/var/lib/athena-alpha-m1/backups` | Every transaction directory recursively, including hidden entries, manifests, records, payload/sidecar files, markers and rollback statuses. |
| `/var/lib/athena-alpha-m1/systemd-canary.json` | Current canary receipt; archived receipt names are under the recursively inventoried state tree if present. |
| `/var/lib/athena-alpha-m1/admin-install.lock` | Installer serialization lock. |
| `/var/lib/athena-alpha-m1` | Complete state-root inventory, including receipt archives and any additional state artifacts. |
| `/run/athena-alpha-m1` | Runner lock, fixture lock, staging root, staged run directories and manifest metadata. `/run` is volatile; an absent path is recorded. |
| `/root/athena-alpha-m1-stage` | Installer staging source tree, recursively, including runner/support/sudoers/canary candidate files. Known sensitive or executable named files are never opened for digesting. |
| `/opt/athena-qualification/build` | Runner’s pinned build tree; complete inventory, with executable file contents not copied. |
| `/var/tmp/athena-d67-ext4` | M27-reported D6 parent, recursively inventoried without following links. |

Transaction contents from current source include `runner`, `support`, `sudoers` backup payloads when originals existed; corresponding `.mode`, `.sha256`, `.owner-group`; exactly one of `{runner,support,sudoers}.{existed,absent}` per target; `backup-manifest.json`; `transaction.json`; `prepared`; `install-started`; optional `install-partial`, `install-complete`, `rollback-started`, `rollback-partial`, `rollback-complete`; and optional `rollback-{runner,support,sudoers}.status`. Installer publication temporaries are created adjacent to each destination with prefixes `.athena-alpha-m1-runner.`, `.athena-alpha-m1-support.`, `.athena-alpha-m1.`; restore temporaries use `.athena-alpha-m1-restore.`. They are not guaranteed to remain present. The acquisition inventories their parent directories and does not infer transaction success from marker presence.

The runner creates `/run/athena-alpha-m1/{staging,runner.lock,fixture.lock}` and run-specific staged artifact directories containing `manifest.json`. Gateway staging uses hidden `.stage-*` temporary directories before publication. The canary receipt writer may preserve existing receipts as `.systemd-canary-archive.*.json`; the full `/var/lib/athena-alpha-m1` walk covers those.

The M27 bundle reports the D6 parent `/var/tmp/athena-d67-ext4` and residual `/var/lib/athena-alpha-m1/ext4-fixture/athena-d6-8yRDjc`, with `generations`, `projection`, `store`, and `generations/.athena-locks/state.lock`. The fixture root is inventoried recursively so no depth cutoff omits descendants. No D6 contents are moved, mounted, or cleaned.

## Historical installer-version differences

M17, M19, M24, M26 and M27 reports describe receipt/destination/transaction evidence from differing candidate periods. M17 records an operator-reported zero-byte `install-started`, while the current source writes `transaction=<id>\n`; this is a material format discrepancy, not evidence of a known historical version. M17/M19/M24 also describe receipt runner/canary digests `5d6ee…` and `5919ef…`, which differ from current installer pins (`c68bca…` and `9d9d4d…`). M27 explicitly leaves the historical source revision and exact receipt-generating bytes unknown. Therefore the acquisition inventories raw names, file metadata, and eligible hashes but does not assume the current schema validates older data. Preserve unknown and unexpected files as evidence.

The M27 bundle is operator-acquired, internally checksum-consistent evidence, not independently authenticated provenance. Its recorded machine/product identifiers, boot ID, fixture tree, receipt and transaction observations are treated as historical observations. This procedure does not inspect or alter the supplied bundle.

## Acquisition completeness criteria

`COMPLETE` means all fixed roots were attempted, each is either explicitly absent or inventoried fully, every child traversal succeeded, all opened descendants stayed on their allowlisted root's filesystem, no symlink was traversed, metadata reads remained stable during any file digest, and both output files were written and synced. Each allowlisted root may be a distinct intended filesystem (for example, the ext4 state volume); traversal stops and records a boundary if a descendant crosses away from that root device. Absent paths are valid explicit results. `PARTIAL` means any symlink in a selected tree, permission/traversal error, filesystem boundary, or changed-during-read observation; it exits 1. Preflight/output creation failures exit 2. Preserve partial output and do not retry into that directory. A successful exit is not proof that expected installed artifacts exist; inspect every `absent` record.

## Safety analysis

`qualification/m28-readonly-acquisition.sh` requires one explicit new absolute output path. It refuses existing paths, uses `umask 077`, creates mode-0700 output and mode-0600 JSON/checksum files, and creates files exclusively. It checks Debian 13 and VirtualBox vendor identity, recording machine ID, DMI product serial, boot ID and UTC time. A fixed literal allowlist is used; there is no caller-provided administrative path list. The synthetic test remap can only run as non-root and maps those fixed paths under a fixture.

Traversal uses `lstat`, `O_NOFOLLOW`, file descriptors, inode/device metadata and an explicit root-device check. Directory enumeration is recursive without a maximum depth. Symlinks are reported and never followed. Regular files are hashed by descriptor unless their name identifies runner, sudoers, support, canary or backup sudoers content. No file payload is copied. No installed command, runner, canary, installer, collector, systemd, sudo, mount or cgroup operation is invoked. Only the new output directory is written. A checksum manifest hashes `inventory.json`; this detects later changes but does not authenticate acquisition identity or time.

## Tests and results

`bash -n qualification/m28-readonly-acquisition.sh`: PASS. Final `python3 -m unittest -v tests/m28_readonly_acquisition_test.py`: 3 PASS, 1 SKIP, 0 FAIL. Expected/missing paths and generated checksum PASS; symlink no-follow and partial status PASS; output collision/failure exit codes PASS. The inaccessible-permission case was SKIP because the sandbox identity could still read a mode-000 directory. The script has explicit error capture for permission failures, but that branch was not exercised in this environment. Tests never access real protected administrative paths; their fixed path strings are remapped into temporary synthetic fixtures.

## Operator invocation

Review the script and this plan on the target VM, confirm the expected disposable Debian 13 VirtualBox guest and choose a new evidence directory on an approved evidence volume. Do not place output under any inventoried administrative root. Then explicitly invoke:

```sh
sudo ./qualification/m28-readonly-acquisition.sh /mnt/monad-m28-evidence-20261029T180000Z
```

Run from the reviewed repository checkout on the target VM and replace the example `/mnt` destination with the approved evidence volume and a fresh unique name if needed. Do not use an existing directory. Exit 0 means traversal complete (including explicit absences); exit 1 means partial acquisition; exit 2 means preflight or output failure. Preserve output in every case. Do not automatically rerun, repair, rename, or delete partial evidence.

## Evidence verification

On the evidence copy, verify the local manifest with `sha256sum -c SHA256SUMS`, inspect `inventory.json` completion/errors and confirm every allowlisted root appears. Compare the recorded identity and UTC with the approved VM/snapshot record. Review all absent, symlink, filesystem-boundary and error records. Keep original output unchanged; any review notes or independent hashes belong in a separate new directory. The manifest is not a signature, trusted timestamp, or independent provenance.

## Limitations and remaining uncertainties

The script does not query hypervisor UUID/snapshot state, ACLs, mount tables, open file handles, process/cgroup/systemd state, journals, or historical installer source. It records DMI product serial as the requested VirtualBox hardware serial indicator; hypervisor-side identity must be reconciled separately. Metadata and hashes cannot identify actors or establish authenticity. It does not read runner/sudoers contents, and hashes are deliberately omitted for those files and named sensitive source artifacts. The current and historical marker formats remain unreconciled. Inability to read a privileged object produces partial evidence; no protected path was read during preparation.

## Next action

After reviewing the complete script and selecting a new evidence destination on the Debian VM, the operator may run the command above. No privileged acquisition was performed during preparation.
