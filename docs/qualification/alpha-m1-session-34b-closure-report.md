# Alpha M1 Session 34B — pin closure candidate

## Candidate identity

- Installer: `qualification/install-athena-alpha-m1-admin.sh`
- Installer SHA-256: `2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7`
- Manifest: `qualification/alpha-m1-build-reconciliation-session34b-candidate.json`
- Manifest SHA-256: `00fd4036bf0f3873f5c2a0e932c912b4709460fa52c35a1c2564016a92811262`
- Source commit: `abd6bac4b368427b76f6c98522afde928b418939`, branch `main`; the checkout contains pre-existing user changes and untracked evidence.

## Artifact pins

| Artifact | SHA-256 |
|---|---|
| Runner | `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` |
| Gateway support | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` |
| Canary | `9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2` |
| Sudoers draft | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` |

The installer change consists only of the three corresponding pin literal updates. Its SHA-256 matches the Session 34 proposed value exactly.

## Verification and inventory

- Installer syntax (`bash -n`): **PASS**.
- Initial regression run: **24 PASS, 1 FAIL, 0 SKIP, 0 TIMEOUT** (25 tests total). The failing test expected the runner, support, and canary pins to remain mismatched.
- The static test now requires all four installer pins (runner, support, sudoers, canary) to match candidate file SHA-256 values. Its negative fixture copies candidate artifacts into a private temporary directory, deliberately alters only the runner pin in fixture text, and confirms that this mismatch is detected. The installer and manifest were not modified.
- Final regression command from the Session 34 verification report: **25 PASS, 0 FAIL, 0 SKIP, 0 TIMEOUT**. Installer syntax (`bash -n`): **PASS**.
- Protected build inventory: **140 total / 18 privileged**, privileged inventory matches the orchestrator. Its source provenance is `/opt/athena-qualification`, not authenticated to this checkout.
- Isolated build inventory: **147 total / 18 privileged**, privileged inventory matches the orchestrator. The build cache names this checkout as source, which does not prove the binaries were built from the candidate source snapshot.
- These build inventories were queried with CTest `--show-only`; no CTest case was executed.

## Remaining limits and disposition

- No approved Debian VM evidence was recollected or verified in this session. All **18 privileged cases are NOT RUN**; live canary, installation, VM receipt, ownership/ACL, systemd/cgroup, and power-loss behavior are also **NOT RUN**.
- Publication of runner, support, and sudoers uses separate file renames. Each rename is individually atomic; the multi-file update is not atomic and interruption can leave mixed generations requiring manual recovery.
- The non-privileged regression harness does not prove root-owned target behavior or durability after a real host or filesystem crash. Approval JSON remains unsigned and depends on an independent human approval record.
- The D6 residual fixture, receipts, backups, and logs remain preservation evidence and were not modified.
- No `sudo`, privileged test, installation, live canary, commit, or push was performed.
- Security independently reviewed the updated static test and exact final installer and manifest identities; it found no dropped security assertion or blocking issue. The installer and manifest SHA-256 values above were reconfirmed after the test run.
- No administrative promotion or installation was performed. The remaining VM and privilege limitations above still apply; this report does not claim Alpha M1 qualification.
