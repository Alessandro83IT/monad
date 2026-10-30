# Session 38 — Manual VM acquisition plan

**Status: NOT EXECUTED. Human operator and separate read-scope approvals required.** This plan supplements and, for execution, supersedes the Session 37 collector procedure where that procedure does not enumerate its complete read set. The current collector is **not approved for execution yet** because the collector file itself is not pinned and executed as the same verified byte buffer; a hash-then-run sequence from a writable checkout has a replacement race. It does not authorize starting or restoring a VM, privileged reads, tests, canary, installation, rollback, cleanup, or system changes.

## 1. Current stop condition

The Session 38 execution environment identified itself as `debian` with `systemd-detect-virt=container-other`. `VBoxManage` is unavailable. The guest VM and snapshot identity/state therefore cannot be verified here. The build paths exist in this container, but that does not establish that these are the real VM builds. Do not run the collector or checker from this container. Do not infer whether `Athena-Alpha-M1` is running or stopped.

The next operator must have authorized access to the VirtualBox host and an authenticated session to a VM that is already running. If the VM is stopped, stop; do not start it. If host-side identity or approval is missing, stop.

## 2. Host identity check (VirtualBox host only)

After the operator approves the exact host and query scope, run these read-only commands on the host:

```sh
VBoxManage list vms
VBoxManage showvminfo "<approved-VM-UUID>" --machinereadable
VBoxManage snapshot "<approved-VM-UUID>" list --details
```

The UUID must be reviewed against the approved `Athena-Alpha-M1` VM. Record host UTC timestamp, VM UUID/name, VM state, current snapshot UUID/name, snapshot list, operator, command output digest, and source host. If the VM is not already running, do not start it. Do not restore, switch, create, or delete snapshots.

## 3. Authenticated guest preflight

Only connect to the VM identified above through the already authorized, existing session. Do not use `sudo` or change the guest state. Record:

```sh
date -u +%Y-%m-%dT%H:%M:%SZ
hostname
id
systemd-detect-virt
cat /etc/machine-id
cat /proc/sys/kernel/random/boot_id
```

Confirm the guest identity against the host record. A virtualization type or matching hostname alone is not VM identity. Confirm both fixed build paths exist in this guest without reading build contents:

```sh
test -d /opt/athena-qualification/build
stat -c 'type=%F mode=%a uid=%u gid=%g path=%n' /opt/athena-qualification/build
test -d /tmp/monad-s32-verification-final4-20261021
stat -c 'type=%F mode=%a uid=%u gid=%g path=%n' /tmp/monad-s32-verification-final4-20261021
```

If either path is absent, symlinked, has unexpected ownership/mode, or cannot be checked, do not invoke the collector; preserve the result as BLOCKED. These checks are not provenance proof.

## 4. Read-scope approval before collector invocation

The collector is non-root and makes no systemd, cgroup, mount, fixture, D6, build, or repository mutations. It does, however, attempt the following reads and writes one new private evidence directory. Approval of a general “read-only collector” label is insufficient; the human operator must review and explicitly approve this exact complete read set and evidence handling before execution. The collector cannot selectively skip these reads; if any item is excluded, do not invoke it and keep affected checks BLOCKED:

- Host/guest identity, OS/kernel, machine-id, boot-id, systemd version/health, and two systemd JSON inventories.
- `systemctl show` and up to 100 recent journal lines for every discovered Alpha unit. Journal lines are content and may contain sensitive data; the exact journal query is included in the required approval.
- Process list including full argv, `/proc` status/cgroup/cmdline for accessible processes, and cgroup event files tied to validated unit properties. These may expose sensitive command arguments or process metadata; full argv collection is included in the required approval.
- Path component, type/mode/ownership/ACL/mount/space metadata and bounded child-name inventories for `/run/systemd/system`, `/proc/1/cgroup`, `/var/lib/athena-alpha-m1`, fixture, receipt, backups, `/var/log/athena-alpha-m1`, D6 parent/residual, and both build trees. Backup/log traversal is bounded by the collector’s fixed depth and exact filename filters; these searches and names are part of the required approval.
- Receipt SHA-256 if the receipt is an accessible regular file; bounded backup/log searches and ACL/hash reads for named transaction, backup-manifest, and receipt files. These are content hashes (not payload disclosure) but still read file bytes and must be explicitly included in the approval. Fixture and D6 payloads are not read.
- The six candidate hashes; fixed-manifest reconciliation; complete CMake cache text; generated CTest/build file hashes; allowlisted artifact hashes; source path metadata and Git revision observation.
- Writes only to one new mode-0700 evidence child under a pre-existing approved private evidence parent. Evidence includes raw output and may contain sensitive process, journal, path, or build configuration details. Do not copy or transfer it without separate approval.

Path checks for protected state are not an atomic no-follow descriptor walk across every subsequent collector command. The collector checks for symlinks first, but path replacement races remain possible. Before approval, the operator must accept this limitation for the exact paths; if not, do not run it and request a separately reviewed no-follow acquisition implementation. Never use elevated access to make the collector succeed.

The collector uses timeouts that may send TERM to its own read-only query subprocesses when a query hangs. They do not target systemd units or unrelated processes. No `systemctl reset-failed/start/stop/restart/kill`, process kill, cgroup write, mount, or service mutation is invoked. If terminating even a collector-owned query process is outside the approved boundary, do not run this collector.

## 5. Collector execution gate — currently closed

**Do not invoke the current collector.** It is not safe to hash the checkout path and then execute that path: the collector itself is not pinned through exact-byte execution, so replacement between verification and execution remains possible. Session 39 must first provide an independently reviewed wrapper that opens, hashes, and executes the same collector byte buffer, or a separately reviewed immutable execution path. After that control exists, Security must approve the exact wrapper, collector/helper pins, fixed arguments, and complete read scope before execution as the ordinary guest user. The helper must continue to be verified and executed from the exact pinned bytes; never execute it directly from the checkout. A hash mismatch or unsafe path is a stop.

The old Session 36 suggestion to run `ctest --show-only` is superseded: do not run CTest, even in show-only mode, because generated CMake may be evaluated. Do not run any checker directly from the checkout outside the reviewed pinned execution wrapper.

The collector may stop or classify an item BLOCKED/FAIL when access is denied or evidence is incomplete. Preserve the raw results. Do not retry with elevated privileges under this approval.

## 6. Output integrity and review

Record the new evidence directory, operator, start/end UTC timestamps, host and guest identifiers, exact collector/helper hashes, every command status, and limitations. Keep the directory private. Produce per-file SHA-256s and record their output plus a tree digest in the separately controlled operations record; do not write a hash sidecar into the collector output, and do not modify any historical evidence. Use a reviewed no-follow digest utility against the new private directory; avoid check-then-redirection or overwriting output files.

The following code is a proposed read-only digest command. Review and approve its exact text and the evidence directory before use; capture its output in the external operations ledger. It emits a SHA-256 for each regular file and a deterministic tree digest without writing a sidecar or following symlinks.

```sh
python3 -I - /absolute/private/new-evidence-directory <<'PY'
import hashlib
import os
import stat
import sys

root = sys.argv[1]
parts = root.split("/")[1:]
if not os.path.isabs(root) or not parts or any(p in ("", ".", "..") for p in parts):
    raise SystemExit("BLOCKED: evidence root must be canonical absolute path")
dir_flags = os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW
fd = os.open("/", dir_flags)
try:
    for part in parts:
        child = os.open(part, dir_flags, dir_fd=fd)
        os.close(fd)
        fd = child
    if not stat.S_ISDIR(os.fstat(fd).st_mode):
        raise RuntimeError("evidence root is not a directory")

    tree = hashlib.sha256()
    count = [0]
    def walk(directory_fd, prefix):
        for name in sorted(os.listdir(directory_fd), key=os.fsencode):
            name_b = os.fsencode(name)
            relative = name_b if not prefix else prefix + b"/" + name_b
            before = os.stat(name, dir_fd=directory_fd, follow_symlinks=False)
            if stat.S_ISLNK(before.st_mode):
                raise RuntimeError("symlink in evidence tree: " + os.fsdecode(relative))
            if stat.S_ISDIR(before.st_mode):
                child_fd = os.open(name, dir_flags, dir_fd=directory_fd)
                try:
                    opened = os.fstat(child_fd)
                    if (opened.st_dev, opened.st_ino) != (before.st_dev, before.st_ino):
                        raise RuntimeError("directory changed during walk: " + os.fsdecode(relative))
                    walk(child_fd, relative)
                finally:
                    os.close(child_fd)
                continue
            if not stat.S_ISREG(before.st_mode):
                raise RuntimeError("special file in evidence tree: " + os.fsdecode(relative))
            file_fd = os.open(name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK,
                              dir_fd=directory_fd)
            try:
                opened = os.fstat(file_fd)
                if not stat.S_ISREG(opened.st_mode) or (opened.st_dev, opened.st_ino) != (before.st_dev, before.st_ino):
                    raise RuntimeError("file changed during walk: " + os.fsdecode(relative))
                file_hash = hashlib.sha256()
                tree.update(len(relative).to_bytes(8, "big"))
                tree.update(relative)
                tree.update(opened.st_size.to_bytes(8, "big"))
                total = 0
                while True:
                    block = os.read(file_fd, 1024 * 1024)
                    if not block:
                        break
                    total += len(block)
                    file_hash.update(block)
                    tree.update(block)
                after = os.fstat(file_fd)
                old = (opened.st_dev, opened.st_ino, opened.st_size, opened.st_mtime_ns, opened.st_ctime_ns)
                new = (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns)
                if total != opened.st_size or old != new:
                    raise RuntimeError("file changed while hashing: " + os.fsdecode(relative))
                print(file_hash.hexdigest() + "  " + os.fsdecode(relative))
                count[0] += 1
            finally:
                os.close(file_fd)
    walk(fd, b"")
    print("files=%d tree_sha256=%s" % (count[0], tree.hexdigest()))
except (OSError, RuntimeError) as exc:
    print("BLOCKED: " + str(exc), file=sys.stderr)
    raise SystemExit(2)
finally:
    os.close(fd)
PY
```

Review `checks.tsv`, all raw evidence and both `build-*-ctest-inventory.json` plus `build-*-ctest-helper-sha256.txt` sidecars. Any unsupported generated CTest syntax, including `subdirs(...)`, remains BLOCKED; do not substitute a partial inventory. Review any sensitive output in place and do not copy it elsewhere without separate approval.

The collector’s approved scope, if separately granted, includes journal content and reading bytes solely to hash the receipt and specifically named backup/log evidence; it does not include fixture or D6 payload reads. If the human approval excludes any automatic collector read, do not invoke the collector; preserve the affected controls as BLOCKED and request a separately reviewed scope/tool that performs only the approved subset. Any administrative read, privilege escalation, cleanup, test, canary, installation, rollback, or state-changing recovery requires separate authorization and is not implied.

## 7. Session 39 entry conditions

Session 39 may reassess controls only after the VM/snapshot identity is bound, the approved read scope is recorded, and a fresh evidence directory is acquired without mutation. Generated CTest inventory must be based on actual generated files; unsupported syntax stays BLOCKED. Build provenance requires an authenticated source/build/output link, not matching hashes alone. Preserve Session 35 and all later evidence directories byte-for-byte.
