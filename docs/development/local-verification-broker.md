# Monad local verification broker (Implementation M1)

## Status and trust boundary

This tree contains a review candidate only. No service, socket, sudoers entry, or
Codex MCP configuration has been installed or activated. The available agent
runtime is an isolated container: its PID 1 is `codex`, `systemd-detect-virt`
reports a container, and it cannot connect to the system bus. A user-provided
interactive VM session previously reported PID 1 `systemd`, virtualization
`oracle`, and Codex CLI 0.163.0. Those observations are not transport evidence:
the broker must remain unconfigured until an administrator captures the VM
machine-id digest and an end-to-end local executor-to-socket test reports that
same VM identity.

Threat model: an MCP agent may be compromised and may submit malformed,
replayed, oversized, slow, or adversarial requests. It is not trusted to supply
an identity, path, executable, or command. The broker trusts kernel peer
credentials, checks that the connecting PID is in the broker's PID and user
namespaces, and pins the machine ID digest, virtualization type, and PID 1
name. This does not defend against a compromised VM kernel, root broker,
systemd, or administrator; nor does a digest prove that the measured file
cannot change after measurement.

## Architecture

The host service listens only on
`/run/monad-local-broker/broker.sock` as `AF_UNIX/SOCK_SEQPACKET`. It admits
only the configured UID and exact host PID/user namespace, limits active
connections to four and requests to 60 per minute, and refuses to replace an
existing socket. Requests use protocol version 1, a canonical UUID, sequence
1, a fresh per-connection challenge, a closed operation name, and operation
specific exact argument keys. Duplicate JSON keys, unknown fields, stale
challenges, and repeated request IDs are rejected. Packets and responses are
bounded; protocol and operation errors use explicit codes. The broker writes
request ID, operation, peer credentials, result status, and elapsed time to
journal stderr; it never logs file contents.

`qualification.local_broker.mcp_server` is a stdio MCP adapter and the local
executor. It forwards only the eight listed tools through the fixed socket.
The optional Codex configuration in
`.codex/local-broker-mcp.toml.example` is disabled template material. Do not
copy it to the active Codex configuration until actual VM transport has been
proven. MCP stdio starts in the Codex session environment, which may still be
the container.

## Operations and semantics

| Operation | Output | Limitations |
| --- | --- | --- |
| `systemd_status` | `systemctl is-system-running` state and return code | Fixed executable and argv; bounded child timeout/output. |
| `failed_units` | Failed unit names/states | Fixed executable and argv; bounded output. |
| `artifact_metadata` | Type, mode, uid/gid, size, device/inode/link count | Fixed identifiers only; no file contents. |
| `artifact_hashes` | SHA-256, measured size, candidate-pin match | Hash proves bytes observed during this read only, not immutability. |
| `fixture_mount` | Mount filesystem/source/options plus PID 1 mount namespace ID | Reads `/proc/1/mountinfo` because systemd hardening gives the service a private mount view. Validate this source assumption on the target VM before relying on it. |
| `fixture_inventory` | Bounded names and metadata only | At most 128 entries; symlinks are listed but never followed. |
| `build_inventory` | Privileged CTest entries and executable presence | Static parser only; it fails closed if generated syntax or subdirectories would make the result incomplete. No CMake or test is run. |
| `environment_identity` | Hostname, boot ID, machine-id digest, kernel, PID 1, virtualization, euid/gid, namespaces | Identity evidence, not attestation against a compromised kernel. |

Paths and commands are constants in `operations.py`; client input cannot select
either. Paths are walked from `/` with `openat`-style directory descriptors and
`O_NOFOLLOW`. System queries use `Popen(argv, shell=False)` with a small fixed
environment, bounded pipes, and a timeout. There are no write operations.

The service runs as root with an empty Linux capability bounding set, no new
privileges, a read-only system view, no home access, no device access, and only
the runtime socket directory writable. Root is currently retained because the
required systemd bus and sudoers hash need a trusted host view. A future split
could serve environment, build, and non-sensitive metadata under the ordinary
account, with only systemd and protected-file reads in the root broker; that
would require separate authenticated endpoints and review. Do not grant a
general shell or wildcard sudo rule.

## Candidate checks before activation

The example service expects the Python package under
`/usr/local/lib/monad-local-broker`, a reviewed checksum manifest at
`/etc/monad-local-broker/code.sha256`, and a root-owned configuration there.
The checksum manifest is a trust root: `sha256sum --check` is useful only if
the manifest itself and every parent directory are root-owned, ACL-free, and
not writable by the broker client. The candidate manifest must be independently
compared to the reviewed source before an administrator installs it. Do not
generate the trusted manifest from whatever happens to be in the install
directory.

Before activation, an administrator should:

1. Review the source diff, dependency imports, unit, config, and candidate
   hashes. Preserve all existing reports and backups.
2. On the actual VM, record `id`, `systemd-detect-virt`, `readlink /proc/1/ns/{pid,user,mnt}`,
   `readlink /proc/self/ns/{pid,user,mnt}`, `/proc/1/comm`, systemd bus access,
   and the machine-id digest. Approve the machine-id digest out of band and
   fill the server and client templates with that exact value.
3. Install the reviewed package, config, manifest, and unit using root-owned
   directories and files. Ensure no symlink or ACL permits the ordinary user
   to replace any parent, module, config, manifest, unit, or executable.
4. Run `systemd-analyze verify` on the unit and verify the mounted view and
   fixed paths. Then start the service during a supervised maintenance window.
5. From the intended Codex session, make one request for each read-only tool.
   Check `environment_identity` against the interactive VM identity, verify
   `/proc/1/mountinfo` corresponds to the VM init mount namespace, and compare
   hashes with independently approved pins. Record the result and stop on any
   mismatch, timeout, incomplete response, or unexpected output.
6. Only after end-to-end transport checks pass, consider enabling the optional
   MCP entry. This broker does not approve Alpha M1 or authorize a privileged
   qualification campaign.

No `sudoers` edit or gateway installation is part of this procedure. The
service template is not a ready-to-run installer; it must be reviewed and
installed by an administrator. Service startup remains fail-closed while the
machine identity pin is a placeholder.

## Install, stop, rollback, and recovery runbook

All commands below are proposals for an approved administrator session; none
has been run by this implementation. First create a VM snapshot or an
administrator-controlled backup of the existing unit/config/package paths,
record ownership, modes, ACLs, xattrs, hashes, and service state, and verify
that the recovery copy is readable. Never overwrite existing files silently.

After artifact review and explicit approval, install the files atomically as a
complete versioned release directory, then switch a root-owned symlink only
when all package hashes and config checks pass. Because multiple independent
files cannot be replaced atomically, stage in a new versioned directory, keep
the prior version untouched, validate the whole staged tree, and make a single
atomic release-pointer switch. Unit/config changes should be installed with
same-filesystem temporary files plus `rename(2)`; preserve prior copies and
metadata. Run `systemd-analyze verify`, `systemctl daemon-reload`, then
`systemctl start monad-local-broker.service`. Verify process identity, socket
owner/group/mode, active status, journal audit records, and all eight read-only
operations before MCP integration.

For deactivation, an administrator can run:

```sh
systemctl stop monad-local-broker.service
systemctl status --no-pager monad-local-broker.service
```

If it was explicitly enabled, disable it separately after approval. The
systemd runtime directory and socket are removed with service stop; preserve
journal logs. Do not delete the package, manifest, config, or backups as part
of ordinary deactivation.

For recovery, stop the service, capture `systemctl status`, `journalctl -u
monad-local-broker.service`, file metadata, and hashes, then restore the prior
versioned package pointer/config/unit from the administrator backup. Verify
the restored hashes and unit before starting it. If validation fails, leave
the service stopped and preserve all evidence. A rollback that changes
administrative state requires explicit administrator approval.

## Test and release status

`tests/local_broker_test.py` is intended to run as an ordinary user with
temporary files and local Unix sockets only. It does not start the systemd
service, access the live VM, invoke sudo, or run privileged tests. Test results
from this container do not establish host transport.

Current candidate checks: 29 unprivileged unit tests PASS; `py_compile`,
candidate `code.sha256.example` verification, and `git diff --check` PASS. The
service template also passes `systemd-analyze verify` when copied to a
temporary `.service` filename in this environment. The first check exposed an
unsupported `RuntimeDirectoryGroup` directive; the unit now uses
`Group=alessandro` for the runtime directory and socket group. Re-run the unit
check on the target VM's systemd version before install. The
test suite covers request validation, duplicate/replay denial, UID and
namespace rejection, bounded results/timeouts, path traversal and symlinks,
fixed system commands, metadata/hash behavior, fixture inventories, CTest
parser failure modes, MCP allowlisting, incomplete/tampered responses, and
transport interruption. It does not test a live systemd bus or the protected
host paths, and does not launch the service because root/config/socket startup
is an administrator-only lifecycle.

One additional read-only parser check against the sandbox-visible
`/opt/athena-qualification/build/CTestTestfile.cmake` returned 18 privileged
test records with CTest file SHA-256
`99751015487882864d67b948dbbb0eee4ac7204aca1b7ee20435f8e69b656bd5`. This is
an observation of the agent-visible mount and is not evidence that it is the
target VM build. No test executable was run.

Security's independent read-only review found no remaining material protocol,
path, or command execution issue in the reviewed candidate. Activation still
requires review of the root-owned code tree and checksum manifest, an approved
out-of-band machine-id digest, and an end-to-end run on the actual VM proving
the local transport and PID 1 mount-namespace source. Until then this is not a
usable host verification channel and does not approve Alpha M1.
