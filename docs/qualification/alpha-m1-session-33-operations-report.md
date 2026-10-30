# Alpha M1 Session 33 — Operations report

## Scope and outcome

Prepared an administrative-only procedure in [alpha-m1-session-33-admin-procedure.md](alpha-m1-session-33-admin-procedure.md). No administrative procedure was executed. No component was installed, no privileged test or canary was run, no VM state was changed, and no fixture, D6 residual, log, receipt, or backup was removed or moved.

This report records three evidence scopes separately:

1. **VM observations supplied in the session request:** 15 failed `athena-alpha-m1-*.service` units with `MainPID=0` and empty `ControlGroup`; installed runner SHA-256 `5d6ee35070954d97034de7f0323204995fe0ca86ed12fad774a3acb9ada021d5`; candidate runner SHA-256 `2290b78c55c72489c0e7dc3bb1c1fedbc573e6bc605e690b6afd3a8ab326b24c`; gateway support and canary absent in expected installed locations; root-owned mode-0700 ext4 fixture and `/var/tmp/athena-d67-ext4`; D6 residual `athena-d6-8yRDjc` with `generations`, `projection`, `store`, and `generations/.athena-locks/state.lock`; protected build with 18 privileged cases; canary candidate hash `e926d2801e2923a38598f23f2a3d94d35303e2e68ee9e833c7e13b0e2a5c4592`; old canary pin `0c2567320d3f5c461d0c9a4a19beaf62b83615c511c28e699810bbdfa49f3107`; isolated Session 32 build reported 147 tests (118 PASS and 11 SKIP among nonprivileged tests). These are reported observations and were **not independently re-collected by this agent**.
2. **Repository/manifest facts:** `qualification/alpha-m1-build-reconciliation-20261021T092750Z.json` declares a 140-test protected build and 18 privileged cases. Runner, gateway support and sudoers hashes match the installer pins; candidate canary hash does not match the old installer pin. The manifest is repository input and does not itself prove the VM or protected build state.
3. **Operations-agent direct observations:** current workspace is `/home/alessandro/Projects/monad`, branch `main`, HEAD `abd6bac4b368427b76f6c98522afde928b418939`; many pre-existing modified/untracked files and build/evidence directories were present. No such artifacts were changed or cleaned by this agent. No direct VM access or VM hash verification was performed.

## Readiness and residual risk

The supplied unit status does not prove descendant termination. Missing `cgroup.events` alone is insufficient evidence, so the procedure requires independent systemd, cgroup and process correlation and stops on ambiguity. D6 is explicitly designated preservation evidence; it must remain in place and cannot be automatically cleaned or relocated.

The canary pin remains intentionally unreconciled. The VM canary/receipt, staged hashes, protected-build identity, sudoers validation, and process-residual check remain unverified by this agent. Therefore this operations deliverable does not by itself support an overall readiness decision; Security and Core review plus final coordinator integration remain required.

## Procedure content delivered

The runbook covers target/snapshot identity, read-only baseline capture, failed-unit and descendant investigation, fixture/mount/D6 retention, exact hash approval and symlink-safe root staging, sudoers and unit review, canary/receipt checks, backup and per-file atomic install limitations, 18-case campaign preparation, evidence retention, explicit stop criteria, and separately approved rollback/recovery. It identifies `SIGKILL` and power-loss windows and forbids automatic retry or cleanup after uncertain or partial state.

No new read-only script was added. The existing VM collector `qualification/alpha-m1-vm-preflight.sh` is referenced as an initial inventory aid only; a successful collector exit means collection completed, not that its findings passed review.

## Operational disposition

**Administrative actions performed:** none. **Privileged tests performed:** none. **D6 status:** preserved by instruction; contents/state not directly accessed. **Recovery/qualification:** not executed and not established. The exact residual prerequisites are listed in the runbook's “Current known gates” section.
