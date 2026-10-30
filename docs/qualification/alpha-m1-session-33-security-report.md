# Alpha M1 Session 33 — independent security review

**Disposition: BLOCKED.** The reviewed cgroup termination fix closes the earlier static cgroup finding for the exact artifacts listed below. The installer still performs an unapproved automatic rollback after post-marker install failure, so the administrative candidate is not ready for approval. This is a static review of the repository checkout in the container, not an observation of the Debian VM. No privileged command, installation, canary run, VM mutation, or privileged test was performed. The provided D6 residual remains designated evidence; its contents and metadata were not directly observed here.

## Reviewed candidate identity

These hashes were read from the checkout after Core's cgroup changes. They identify the reviewed local files, not files observed on the VM:

| File | SHA-256 |
|---|---|
| `qualification/athena-alpha-m1-systemd-canary.py` | `caeb095d1e9a5f3bb631a415f49f0f039ab36874352cba43a86c0a6207440893` |
| `qualification/athena-alpha-m1-runner.py` | `c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def` |
| `qualification/gateway_support.py` | `92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514` |
| `qualification/install-athena-alpha-m1-admin.sh` | `55f13e8d1d91e149b6f24a8257fa48339f2b48e2a1b619f5ae0a5b1053d463a8` |
| `qualification/athena-alpha-m1-sudoers` | `263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18` |

The canary source hash differs from the installer pin, which remains `0c2567320d3f5c461d0c9a4a19beaf62b83615c511c28e699810bbdfa49f3107`. Do not stage or generate a receipt from this candidate until the pin has been explicitly reviewed and promoted.

## Termination review

### Closed by reviewed candidate: missing cgroup evidence is no longer accepted on stop/error paths

The prior candidate accepted `ABSENT` on ordinary terminal states. In the reviewed candidate, canary termination accepts an empty/removed cgroup only when the current `systemd-run` returned zero and `systemctl` reports the expected unit ID, inactive/failed state, PID zero, and `Result=success`; stop, timeout, interruption, and error paths do not enable that exception ([canary](../../qualification/athena-alpha-m1-systemd-canary.py#L347), [canary](../../qualification/athena-alpha-m1-systemd-canary.py#L401)). An empty path or missing cgroup on other paths remains unconfirmed, so cleanup and receipt publication are denied.

The runner now requires systemd's `Id` to match the requested transient unit and its `ControlGroup` to equal `/system.slice/{Id}` or be empty; it maps missing cgroup evidence to indeterminate (`None`) rather than empty ([runner](../../qualification/athena-alpha-m1-runner.py#L235), [runner](../../qualification/athena-alpha-m1-runner.py#L256)). The shared termination predicate permits a removed cgroup only when the caller explicitly proves successful completion and systemd reports `Result=success` ([gateway support](../../qualification/gateway_support.py#L223)). This addresses the supplied failed-unit / empty-ControlGroup case statically.

Core reports the focused canary/admin and gateway tests pass (24 and 15 respectively), plus `py_compile`. I inspected the related source and regression cases but did not execute tests independently. The added cases cover empty/missing cgroup evidence, unsuccessful result, exact unit/cgroup identity, and successful completion before a removed cgroup is accepted. Runtime systemd/cgroup behavior remains unverified on the VM.

### 2. Installer performs rollback without a separate approval step

The installer's EXIT trap automatically invokes `rollback_all` after a nonzero exit if `install-started` exists ([installer](../../qualification/install-athena-alpha-m1-admin.sh#L503)). This changes installed administrative state as part of the `install` invocation. The authorization policy requires human approval for state-changing rollback ([authorization policy](../security/authorization-policy.md)), and this session requires rollback to be explicitly approved. A general approval to attempt installation does not provide a review point after a partial failure.

**Required mitigation:** do not restore targets automatically from the install failure trap. Preserve the backup, marker, temporary/partial state, and a failure report; stop and require an explicit, separately approved `rollback` invocation after manifest and destination review. The existing per-file rename limitation and SIGKILL/power-loss recovery notes must remain explicit.

## Other security observations

- **Promotion and rollback atomicity:** runner, support module, and sudoers are swapped/restored by separate renames ([installer](../../qualification/install-athena-alpha-m1-admin.sh#L568)). The runbook correctly identifies that SIGKILL, power loss, or I/O failure can leave mixed generations, and says to preserve state and recover manually ([staging procedure](alpha-m1-admin-staging.md#L144)). This is a known limitation, but the automatic rollback trap above makes the failure behavior less approval-controlled. Retain an immutable copy of the approved artifact manifest and record exact target hashes after any partial state.
- **Backup and receipts:** the installer creates an aggregate backup manifest and validates its inventory before explicit rollback ([installer](../../qualification/install-athena-alpha-m1-admin.sh#L314), [installer](../../qualification/install-athena-alpha-m1-admin.sh#L457)). The canary archives the existing receipt by a same-directory hard link before replacing it and serializes publishers with a lock ([canary](../../qualification/athena-alpha-m1-systemd-canary.py#L183), [canary](../../qualification/athena-alpha-m1-systemd-canary.py#L217)). These mechanisms detect many accidental changes, but local root can alter both data and manifests; they are not immutable audit evidence. Preserve every receipt, backup, and log and compare against independently retained hashes.
- **Path and symlink handling:** runner artifact staging uses descriptor-relative, no-follow opens and verifies source/staged hashes and identities ([gateway support](../../qualification/gateway_support.py#L73)). The installer validates administrative path components before mutation ([installer](../../qualification/install-athena-alpha-m1-admin.sh#L97)). The canary's receipt check opens the final parent directory with `O_NOFOLLOW`, but does not walk and reject symlinks in every ancestor component ([canary](../../qualification/athena-alpha-m1-systemd-canary.py#L145)). The installer later checks its path chain, reducing installation impact, but the canary can still read/write a receipt through an ancestor symlink before that check. Use descriptor-relative component walking for the receipt path and keep the installer check as a second gate.
- **Privilege scope:** sudoers is restricted to 18 exact runner arguments and excludes the canary ([sudoers draft](../../qualification/athena-alpha-m1-sudoers#L1)). The runner enforces a matching allowlist, pins build artifact hashes, stages files into a private directory, and limits systemd capabilities. I found no generic shell or implicit sudoers expansion in the reviewed draft. Runtime behavior and the effective installed policy remain unverified on the VM.
- **D6 evidence:** the staging procedure explicitly marks `/var/tmp/athena-d67-ext4` inventory as not verified in this container and instructs the administrator to inventory it read-only without deletion or modification ([staging procedure](alpha-m1-admin-staging.md#L115), [staging procedure](alpha-m1-admin-staging.md#L140)). This is the correct preservation boundary. Do not change, move, clean, mount over, or reuse the D6 residual. The separate ext4 fixture must be checked independently before promotion.
- **VM prerequisites:** no container observation proves the VM identity/snapshot, installed artifact hashes, systemd behavior, ownership/ACLs, cgroup state, mount options, receipt validity, or D6 inventory. These remain explicit administrative read-only checks. No claim of Alpha M1 qualification follows from this review.

## Updated review disposition

The reviewed files have useful path validation, artifact staging, receipt retention, structured evidence, exact authorization controls, and a fail-closed static cgroup termination check. However, the installer EXIT trap still automatically invokes `rollback_all` after an install failure. That is a separate state-changing rollback without a separate approval point and remains a blocking authorization defect. The canary pin is also stale relative to the reviewed hash. Correct the rollback behavior through reviewed changes, reconcile all hashes and manifest inputs without automatic pin updates, and obtain a fresh independent review before presenting the candidate for administrative approval. The static cgroup fix does not qualify runtime behavior or Alpha M1.
