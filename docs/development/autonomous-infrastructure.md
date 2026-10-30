# Autonomous development infrastructure

## Components

- `AGENTS.md` is the repository-wide working contract read by Codex.
- `.codex/config.toml` enables native multi-agent tools and caps concurrent spawned agents at four.
- `.codex/agents/{core,verification,security,operations}.toml` define the four bounded project roles. Agents inherit the coordinator's permissions; these files are guidance, not a security boundary.
- `scripts/codex-autonomous.sh` starts Codex from the repository root and forwards normal CLI arguments. It does not select a bypass or approval mode.
- `qualification/athena-alpha-m1-runner.py` plus `gateway_support.py` form the narrow Alpha M1 root runner candidate. Its sudoers allowlist exposes only 18 fixed test cases. They are not a general administrative gateway and have not been installed by this work.

Codex officially supports project-scoped custom TOML agents under `.codex/agents/` and the `[agents]` settings used here. These files parse as TOML and are covered by local consistency tests. The Codex executable is absent from this environment (`command -v codex` and `codex --version` both unavailable), so CLI version and runtime loading could not be verified; confirm those when activating on the Debian VM. See [official Codex subagent documentation](https://developers.openai.com/codex/multi-agent/) and the [configuration reference](https://developers.openai.com/codex/config-reference/).

## Coordinator workflow

1. Record branch, `HEAD`, and complete working-tree status before edits; preserve pre-existing changes.
2. Inspect build configuration, relevant tests, and existing qualification evidence.
3. Break work into independent bounded paths. Delegate implementation, verification, security review, and operations review where useful; avoid overlapping writes.
4. Integrate only after agents report their files and evidence. Run requested targeted checks and record exact outcomes.
5. Keep privileged qualification outside routine development. The coordinator cannot convert a skipped or unavailable privileged check into a pass.

Start a session from the repository with:

```sh
./scripts/codex-autonomous.sh
```

Pass ordinary Codex CLI options after the script. This command starts a normal Codex session; it does not bypass approval controls. The current checkout contains pre-existing Alpha M1 edits and evidence and the CLI is absent in this environment.

Run the local infrastructure checks with:

```sh
python3 scripts/test_autonomous_infrastructure.py
ctest --test-dir build-monad-debug --output-on-failure \
  -R '^(qualification_driver_test|alpha_m1_canary_child_test|alpha_m1_admin_static_test|alpha_m1_orchestrator_test)$'
```

## Gateway boundary and promotion

The only current privileged command design is the Alpha M1 runner and its exact-argument sudoers draft. It opens source artifacts relative to a verified build directory FD, copies the executable and library into a unique root-owned `/run` stage, verifies source identity across copy and rehashes the staged objects, then executes only the staged paths. A per-run manifest records source and staged device/inode identities and hashes. The helper is separately hash-pinned by the runner and installer. A locked `flock` wrapper in the unit holds the fixture lock until the test process exits. The runner waits for systemd inactive/failed state, zero main PID, and an unpopulated cgroup before checking fixture cleanup or reporting completion. Unknown termination preserves the stage and fixture and is classified `TERMINATION_UNCERTAIN`.

The runner is **READY FOR ADMIN REVIEW**, but not promoted or activated. The independent Security review and complete non-privileged regression suite are complete. Root-owned evidence and its SHA-256 manifest protect against unprivileged edits and detect accidental changes only when compared with a separately recorded digest; root on the same host can alter both. No same-host root immutability is claimed. Installation/rollback is administrative state mutation and remains human-run after explicit review. No gateway installation or root operation was performed here.

## Evidence and qualification

Alpha M1 currently remains INCOMPLETE. Existing reports document the `RestrictSUIDSGID`/`openat2` diagnosis and `nosuid` canary, exception-safe D4/D5/D6 fixture cleanup, result classification, and evidence retention. Keep those source changes and all generated evidence intact. Do not remove the residual D6 fixture. A local unprivileged test result is not a privileged campaign result.

See the [authorization policy](../security/authorization-policy.md), [recovery procedure](../qualification/alpha-m1-recovery.md), and existing [Alpha M1 admin staging runbook](../qualification/alpha-m1-admin-staging.md).
