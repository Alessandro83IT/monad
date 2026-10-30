# Alpha M1 Session 35 — Core candidate integrity report

## Scope and environment

This report covers read-only review of the Session 34B installer candidate,
manifest, four pinned support artifacts, and the sudoers draft. No candidate,
installed administrative path, VM state, or build artifact was changed. The
available environment is the repository checkout at `/home/alessandro/Projects/monad`
on branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`. The checkout
has pre-existing modified and untracked files; those were preserved. This
inspection does not establish that this environment is the Debian qualification
VM.

## Candidate and pin reconciliation

Command:

```sh
sha256sum qualification/install-athena-alpha-m1-admin.sh \
  qualification/alpha-m1-build-reconciliation-session34b-candidate.json \
  qualification/athena-alpha-m1-runner.py qualification/gateway_support.py \
  qualification/athena-alpha-m1-systemd-canary.py qualification/athena-alpha-m1-sudoers
```

**PASS** — the output reproduced the approved identities:

| File | Observed SHA-256 | Result |
|---|---|---|
| Installer | `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7` | PASS; matches baseline |
| Session 34B candidate manifest | `00fd4036bf0f3873f5c2a0e932c912b4709460fa52c35a1c2564016a92811262` | PASS; matches baseline |
| Runner | `c68bca67a7a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` | PASS; matches baseline and installer literal |
| Gateway support | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` | PASS; matches baseline and installer literal |
| Canary | `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` | PASS; matches baseline and installer literal |
| Sudoers draft (additional installer pin) | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` | PASS; matches installer literal and Session 34B report |

The manifest's `admin_artifact_hashes_vs_installer_pins` records matching
artifact and pin values for runner, gateway support, sudoers draft, and canary.
It also records build artifact digests and build provenance claims; those
claims were not independently validated by this Core scope. No security pin
was regenerated or changed.

## Installer review

Command: `bash -n qualification/install-athena-alpha-m1-admin.sh`

**PASS** — syntax check exited 0. The installer SHA-256 during the check was
`2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7`.

Read-only source inspection used `rg -n` on the installer and `sed` over its
path-validation, rollback, lock, and EXIT-trap sections. Results:

- **PASS (source inspection):** the EXIT handler records `install-partial`
  after publication has started and does not call rollback. Pre-publication
  temporary files are removed only before the durable `install-started`
  marker. This is source evidence, not a live fault-injection result.
- **PASS (source inspection):** rollback is a separate explicit action with
  transaction ID, approval file, and approval SHA-256 arguments. The approval
  is bound to transaction and backup metadata, target hashes, and action;
  the approval remains unsigned, so it does not independently authenticate the
  human approver.
- **PASS (source inspection):** a persistent lock file is checked for regular
  non-symlink type, owner/mode/link count and acquired with nonblocking `flock`.
  Existing incomplete transactions block another install.
- **PASS (source inspection):** fixed absolute paths are checked for normalized
  components, symlinks, ownership, writable/special mode bits, and ACLs. The
  fixture path is rejected if it is a symlink; the fixture is required to be
  ext4. These checks were not applied to the live VM in this Core task.
- **PASS (source inspection):** receipt validation requires mode 0600 and
  validates metadata, schema and artifact/runner/boot/systemd bindings; path
  traversal uses no-follow descriptor-relative opens. No live receipt was
  accessed here.
- **PASS (source inspection):** sudoers draft enumerates 18 exact runner and
  CTest argument pairs, with no wildcard or shell command. Installer stages it
  mode 0440 and invokes `visudo -cf` before publication outside test mode.
  `visudo` was not run here.

The source conclusions above are corroborated by the Session 34B closure report,
but are not a substitute for Verification's requested static regression suite
or Security's independent review. No regression tests were run by Core.

## Outcome and limits

Core candidate-integrity gate: **PASS** for the supplied baseline hashes,
current installer syntax, pin literals, and inspected source controls.

The VM state, installed copies, current receipts, ACLs/ownership on the VM,
systemd/cgroup residuals, snapshot identity, and protected build provenance
remain outside this report's observable scope (**NOT RUN** here). Build
artifact consistency with the Session 34B manifest is therefore **NOT
INDEPENDENTLY VERIFIED** by Core. The session's overall administrative decision
must remain **BLOCKED** if any VM, build-provenance, or independent Security
gate is not satisfied. This report does not imply staging approval or Alpha M1
qualification.
