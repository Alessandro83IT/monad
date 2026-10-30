# Authorization policy

This policy describes what a development agent may do. Repository instructions are not an OS security boundary; filesystem sandboxing, exact sudoers entries, protected ownership, and human review enforce the boundary.

| Operation | Agent policy | Boundary |
|---|---|---|
| Source inspection, implementation, documentation | Allow | Workspace only; preserve existing work |
| Isolated build, non-privileged tests, static checks | Allow | Build directories and fixtures must not overwrite existing evidence |
| Non-destructive evidence capture | Allow | New uniquely named output; preserve source evidence |
| Run an already approved privileged Alpha M1 case | Allow only through verified installed runner | Exact case allowlist, pinned protected artifacts, validated VM/canary; never execute workspace binaries as root |
| Inspect installed state read-only | Allow when path and command are fixed | Do not disclose secrets; fail closed on unexpected paths |
| Modify sudoers, install/update runner or gateway, change policy | Human approval required | Reviewed artifact hashes, backup, syntax validation, explicit activation |
| Expand privileges or alter security controls | Human approval required | Independent security review and approval |
| Delete fixture, logs, backups, or receipts | Human approval required | Preserve qualification evidence; D6 residual is retained |
| Roll back installed administrative state | Human approval required | Verify backup manifest and hashes; preserve backup and recovery log |
| Push, publish, or release | Human approval required | Explicit release authorization |

## Gateway requirements

The privileged interface must accept an operation identifier and a closed set of typed arguments, not a command string. Each operation has a fixed executable, fixed argument shape, fixed protected paths, resource limits, and timeout. Unknown operations or arguments fail closed. No shell, environment-derived executable, mutable build output, generic file path, or policy editing operation is permitted.

Before using an artifact, validate every directory component without following symlinks, owner/mode/ACL, regular-file type, and expected digest. Keep the validated object identity stable through execution (for example, protected immutable staging plus inode/device revalidation and an execution mechanism bound to the verified file descriptor). A path-based hash followed by reopening the path is not sufficient against TOCTOU replacement.

Give each run a random unique ID. Record structured start/end state, exact operation, caller identity, artifact hashes, result classification, timeout, and output digest. Create records exclusively, fsync files and parent directory, and preserve all records. Root-owned files alone are not tamper-proof against root; stronger append-only claims require an independently protected audit facility or signed remote evidence.

Use backups with a manifest that records content digest, owner, group, mode, and file type. Verify the manifest before restoration. Rollback must be an explicit administrative action with a unique run ID; failures leave originals, backups, and partial-state report intact. No automatic deletion or cleanup follows failure.

## Alpha M1 boundary

The runner's 18-case sudoers draft remains narrowly scoped. Artifact staging, unit termination confirmation, fixture locking, and evidence manifests are now implemented in the candidate runner/helper. Keep root test execution separate from administrative installation and rollback. A successful static test or mock does not qualify the VM.

## Evidence threat model

- **Unprivileged user:** root-owned `0700` log and staging directories, exclusive creation, no-follow file opens, and fixed names prevent ordinary users from replacing or editing runner evidence and protected artifacts.
- **Accidental damage:** per-run JSON records, output digests, staged artifact manifests, SHA-256 sidecars, and backup manifests expose truncation, substitution, missing files, or metadata drift when independently rechecked.
- **Alteration detection:** `verify_evidence_manifest` and `validate_backup_manifest` recompute file hashes and compare file identity metadata before use/restoration. Compare the evidence sidecar digest with the separately captured runner marker/report; the verifier does not treat the sidecar's own hash as an external trust anchor.
- **Compromised root:** no local file, hash, owner/mode check, or same-host manifest is immutable against root. Root can rewrite both evidence and its manifest or replace the verifier. Stronger assurance requires a remote append-only audit sink, externally held signature key, or independently acquired evidence.
