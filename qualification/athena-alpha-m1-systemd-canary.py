#!/usr/bin/python3
"""Harmless root-run canary for Athena's proposed systemd confinement.

Run manually on the disposable qualification VM after review. This does not
execute any Athena binary and is intentionally absent from sudoers.
"""

import hashlib
import fcntl
import json
import os
import secrets
import shutil
import stat
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from enum import Enum
from contextlib import contextmanager
from pathlib import Path

SYSTEMD_PATH = "/usr/sbin:/usr/bin:/sbin:/bin"
SYSTEMD_RUN = shutil.which("systemd-run", path=SYSTEMD_PATH)
SYSTEMCTL = shutil.which("systemctl", path=SYSTEMD_PATH)
PYTHON = "/usr/bin/python3"
BUILD = "/opt/athena-qualification/build"
LOGDIR = "/var/log/athena-alpha-m1"
EXT4_PARENT = "/var/lib/athena-alpha-m1/ext4-fixture"
RECEIPT = "/var/lib/athena-alpha-m1/systemd-canary.json"

CHILD = r'''import errno, os, secrets, subprocess, sys
import socket
nonce, host_tmp, host_var_tmp_marker, host_mnt, host_net, host_uts, host_dev_mount, ext4_parent = sys.argv[1:]
if os.path.exists(os.path.join(host_tmp, "host-marker")):
    raise SystemExit("FAIL: PrivateTmp exposed caller's /tmp")
if os.path.exists(host_var_tmp_marker):
    raise SystemExit("FAIL: PrivateTmp exposed caller's /var/tmp")
if os.stat("/proc/self/ns/mnt").st_ino == int(host_mnt):
    raise SystemExit("FAIL: PrivateMounts did not create a mount namespace")
if os.stat("/proc/self/ns/net").st_ino == int(host_net):
    raise SystemExit("FAIL: PrivateNetwork did not create a network namespace")
if os.stat("/proc/self/ns/uts").st_ino == int(host_uts):
    raise SystemExit("FAIL: ProtectHostname did not create a UTS namespace")
dev_mount = None
with open("/proc/self/mountinfo", encoding="ascii") as stream:
    for line in stream:
        fields = line.split()
        if len(fields) > 4 and fields[4] == "/dev":
            dev_mount = fields[0]
            break
if dev_mount is None or dev_mount == host_dev_mount:
    raise SystemExit("FAIL: PrivateDevices did not replace the /dev mount")
status = {}
with open("/proc/self/status", encoding="ascii") as stream:
    for line in stream:
        if ":" in line:
            key, value = line.split(":", 1)
            status[key] = value.strip()
if status.get("NoNewPrivs") != "1":
    raise SystemExit("FAIL: NoNewPrivileges is not active")
# PrivateMounts plus NNP must make writable mounts non-suid. This replaces
# RestrictSUIDSGID, whose seccomp filter also denies openat2 with ENOSYS.
for writable in ("/tmp", ext4_parent):
    options = subprocess.run(["/usr/bin/findmnt", "-n", "-o", "OPTIONS", "-T", writable],
                             check=True, text=True, capture_output=True).stdout.strip().split(",")
    if "nosuid" not in options:
        raise SystemExit("FAIL: writable path is not nosuid inside private mount namespace: " + writable)
allowed_caps = (1 << 0) | (1 << 21)  # CAP_CHOWN and CAP_SYS_ADMIN
if int(status.get("CapBnd", "0"), 16) & ~allowed_caps:
    raise SystemExit("FAIL: capability bounding set contains an unexpected capability")
try:
    os.stat("/home/alessandro")
except OSError:
    pass
else:
    raise SystemExit("FAIL: ProtectHome exposed /home/alessandro")
try:
    socket.socket(socket.AF_INET, socket.SOCK_STREAM).close()
except OSError as e:
    if e.errno not in (errno.EPERM, errno.EAFNOSUPPORT, errno.EPROTONOSUPPORT):
        raise
else:
    raise SystemExit("FAIL: RestrictAddressFamilies allowed AF_INET")
try:
    os.open("/proc/sys/kernel/hostname", os.O_WRONLY | os.O_CLOEXEC)
except OSError as e:
    if e.errno not in (errno.EROFS, errno.EACCES, errno.EPERM):
        raise
else:
    raise SystemExit("FAIL: ProtectKernelTunables allowed opening hostname for write")
private_file = "/tmp/" + nonce + ".private"
with open(private_file, "x", encoding="ascii") as f:
    f.write("private\n")
private_var_tmp_file = "/var/tmp/" + nonce + ".private"
with open(private_var_tmp_file, "x", encoding="ascii") as f:
    f.write("private\n")
for protected in ("/opt/athena-qualification/build", "/var/log/athena-alpha-m1",
                  "/var/lib/athena-alpha-m1", "/var/lib/athena-alpha-m1/backups"):
    options = subprocess.run(["/usr/bin/findmnt", "-n", "-o", "OPTIONS", "-T", protected],
                             check=True, text=True, capture_output=True).stdout.strip().split(",")
    if "ro" not in options:
        raise SystemExit("FAIL: protected path is not read-only: " + protected)
for protected in ("/var/log/athena-alpha-m1", "/var/lib/athena-alpha-m1",
                  "/var/lib/athena-alpha-m1/backups"):
    probe = protected + "/.athena-alpha-m1-readonly-" + nonce + "-" + secrets.token_hex(4)
    try:
        fd = os.open(probe, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except OSError as e:
        if e.errno not in (errno.EROFS, errno.EACCES, errno.EPERM):
            raise
    else:
        os.close(fd)
        os.unlink(probe)
        raise SystemExit("FAIL: sandbox allowed write under " + protected)
ext4_probe = os.path.join(ext4_parent, ".canary-" + nonce)
fd = os.open(ext4_probe, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
os.close(fd)
os.unlink(ext4_probe)
print("PASS: namespaces, NNP/capability bound, home/network/kernel isolation, private /tmp and /var/tmp, read-only paths, dedicated ext4 writable")
'''


def protected_sha256(path):
    digest = hashlib.sha256()
    fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
    try:
        st = os.fstat(fd)
        if (not stat.S_ISREG(st.st_mode) or st.st_uid != 0
                or stat.S_IMODE(st.st_mode) & 0o7022):
            raise RuntimeError("staged canary/runner must be a root-owned protected regular file")
        if any(name in ("system.posix_acl_access", "system.posix_acl_default") for name in os.listxattr(fd)):
            raise RuntimeError("staged canary/runner has an extended ACL")
        while True:
            block = os.read(fd, 1024 * 1024)
            if not block:
                break
            digest.update(block)
    finally:
        os.close(fd)
    return digest.hexdigest()


def _open_directory_nofollow(path):
    """Open an absolute directory path without following any component symlink."""
    path = Path(path)
    if not path.is_absolute() or any(part in ("", ".", "..") for part in path.parts[1:]):
        raise RuntimeError("receipt directory path must be absolute and normalized")
    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW
    fd = os.open("/", flags)
    try:
        for part in path.parts[1:]:
            next_fd = os.open(part, flags, dir_fd=fd)
            os.close(fd)
            fd = next_fd
        return fd
    except BaseException:
        os.close(fd)
        raise


def ensure_receipt_safe(receipt=Path(RECEIPT)):
    """Validate existing evidence without changing or archiving it."""
    parent = receipt.parent
    fd = _open_directory_nofollow(parent)
    try:
        st = os.fstat(fd)
        if not stat.S_ISDIR(st.st_mode) or st.st_uid != 0 or stat.S_IMODE(st.st_mode) != 0o700:
            raise RuntimeError("canary receipt directory must be root-owned mode 0700")
        if any(name in ("system.posix_acl_access", "system.posix_acl_default") for name in os.listxattr(fd)):
            raise RuntimeError("canary receipt directory has an extended ACL")
        try:
            receipt_fd = os.open(receipt.name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=fd)
        except FileNotFoundError:
            return
        try:
            old = os.fstat(receipt_fd)
            if (not stat.S_ISREG(old.st_mode) or old.st_uid != 0 or stat.S_IMODE(old.st_mode) != 0o600
                    or any(name in ("system.posix_acl_access", "system.posix_acl_default")
                           for name in os.listxattr(receipt_fd))):
                raise RuntimeError("existing canary receipt has unsafe metadata")
        finally:
            os.close(receipt_fd)
    finally:
        os.close(fd)


def _receipt_bytes(fd):
    digest = hashlib.sha256()
    chunks = []
    while True:
        block = os.read(fd, 1024 * 1024)
        if not block:
            break
        chunks.append(block)
        digest.update(block)
    return b"".join(chunks), digest.hexdigest()


def _archive_existing_receipt(dir_fd, receipt_name):
    try:
        old_fd = os.open(receipt_name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=dir_fd)
    except FileNotFoundError:
        return None
    try:
        before = os.fstat(old_fd)
        if not stat.S_ISREG(before.st_mode) or before.st_uid != 0 or stat.S_IMODE(before.st_mode) != 0o600:
            raise RuntimeError("existing canary receipt has unsafe metadata")
        if any(name in ("system.posix_acl_access", "system.posix_acl_default")
               for name in os.listxattr(old_fd)):
            raise RuntimeError("existing canary receipt has an extended ACL")
        old_bytes, old_hash = _receipt_bytes(old_fd)
        after = os.fstat(old_fd)
        if (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (
                after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns):
            raise RuntimeError("existing canary receipt changed during archive")
        archive_name = ".systemd-canary-archive." + secrets.token_hex(12) + ".json"
        os.link(receipt_name, archive_name, src_dir_fd=dir_fd, dst_dir_fd=dir_fd, follow_symlinks=False)
        archive_fd = os.open(archive_name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=dir_fd)
        try:
            linked = os.fstat(archive_fd)
            linked_bytes, linked_hash = _receipt_bytes(archive_fd)
            if ((linked.st_dev, linked.st_ino) != (before.st_dev, before.st_ino)
                    or linked_hash != old_hash or linked_bytes != old_bytes):
                raise RuntimeError("archived canary receipt identity/content mismatch")
        finally:
            os.close(archive_fd)
        os.fsync(dir_fd)
        return archive_name
    finally:
        os.close(old_fd)


@contextmanager
def _receipt_publication_lock(dir_fd, timeout=30):
    name = ".systemd-canary.receipt.lock"
    lock_fd = os.open(name, os.O_RDWR | os.O_CREAT | os.O_CLOEXEC | os.O_NOFOLLOW,
                      0o600, dir_fd=dir_fd)
    try:
        lock_stat = os.fstat(lock_fd)
        if (not stat.S_ISREG(lock_stat.st_mode) or lock_stat.st_uid != 0
                or stat.S_IMODE(lock_stat.st_mode) != 0o600 or lock_stat.st_nlink != 1
                or any(x in ("system.posix_acl_access", "system.posix_acl_default")
                       for x in os.listxattr(lock_fd))):
            raise RuntimeError("canary receipt publication lock has unsafe metadata")
        deadline = time.monotonic() + timeout
        while True:
            try:
                fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except BlockingIOError:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("timed out waiting for canary receipt publication lock")
                time.sleep(min(0.05, remaining))
        try:
            yield
        finally:
            fcntl.flock(lock_fd, fcntl.LOCK_UN)
    finally:
        os.close(lock_fd)


def record_success(outcome):
    if (not outcome.terminated or outcome.returncode != 0 or outcome.timed_out
            or outcome.interrupted or outcome.error):
        raise RuntimeError("cannot publish a canary receipt without confirmed successful termination")
    parent = Path(RECEIPT).parent
    ensure_receipt_safe(Path(RECEIPT))
    fd = _open_directory_nofollow(parent)
    try:
        st = os.fstat(fd)
        if not stat.S_ISDIR(st.st_mode) or st.st_uid != 0 or stat.S_IMODE(st.st_mode) != 0o700:
            raise RuntimeError("canary receipt directory must be root-owned mode 0700")
        with _receipt_publication_lock(fd):
            # Recheck after taking the lock; no competing publisher can race this
            # archive/publish sequence or replace a receipt published meanwhile.
            ensure_receipt_safe(Path(RECEIPT))
            payload = json.dumps({"schema": 1, "passed": True,
                                  "canary_sha256": protected_sha256(__file__),
                                  "runner_sha256": protected_sha256(Path(__file__).with_name("athena-alpha-m1-runner.py")),
                                  "boot_id": Path("/proc/sys/kernel/random/boot_id").read_text().strip(),
                                  "systemd_version": subprocess.run([SYSTEMCTL, "--version"],
                                                                     check=True, text=True, capture_output=True,
                                                                     timeout=5).stdout.splitlines()[0],
                                  "validated_ns": time.time_ns()}, sort_keys=True, separators=(",", ":")) + "\n"
            name = ".systemd-canary." + secrets.token_hex(8)
            out = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
                          0o600, dir_fd=fd)
            try:
                with os.fdopen(os.dup(out), "w", encoding="utf-8") as stream:
                    stream.write(payload)
                    stream.flush()
                    os.fsync(stream.fileno())
            finally:
                os.close(out)
            receipt_name = Path(RECEIPT).name
            _archive_existing_receipt(fd, receipt_name)
            # Publish only after prior evidence has a durable unique hard-link archive.
            os.replace(name, receipt_name, src_dir_fd=fd, dst_dir_fd=fd)
            os.fsync(fd)
    finally:
        os.close(fd)


@dataclass(frozen=True)
class UnitOutcome:
    returncode: int
    terminated: bool
    timed_out: bool = False
    interrupted: bool = False
    error: str = ""

    @property
    def preserve_markers(self):
        return self.timed_out or self.interrupted or not self.terminated


def cleanup_allowed(outcome, preserve_markers=False):
    return (not preserve_markers
            and (outcome is None or outcome.terminated))


def _unit_properties(output):
    properties = {}
    for line in output.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        if key in properties:
            raise ValueError("duplicate systemd unit property: " + key)
        properties[key] = value
    return properties


class CgroupState(Enum):
    POPULATED = 1
    EMPTY = 2
    ABSENT = 3
    UNKNOWN = 4


def _cgroup_state(control_group, read_events):
    if control_group == "":
        return CgroupState.ABSENT
    if (not control_group.startswith("/") or control_group == "/"
            or ".." in Path(control_group).parts or "\x00" in control_group):
        return CgroupState.UNKNOWN
    try:
        events = read_events(Path("/sys/fs/cgroup") / control_group.lstrip("/"))
    except FileNotFoundError:
        return CgroupState.ABSENT
    except (OSError, ValueError):
        return CgroupState.UNKNOWN
    populated = [line.split()[1] for line in events.splitlines()
                 if len(line.split()) == 2 and line.split()[0] == "populated"]
    if populated == ["0"]:
        return CgroupState.EMPTY
    if populated == ["1"]:
        return CgroupState.POPULATED
    return CgroupState.UNKNOWN


def confirm_unit_terminated(systemctl, unit, run=subprocess.run, monotonic=time.monotonic,
                            sleep=time.sleep, read_events=None, timeout=15,
                            allow_removed_after_success=False):
    """Boundedly confirm inactive/failed, MainPID=0, and cgroup populated=0."""
    if read_events is None:
        read_events = lambda path: path.read_text(encoding="ascii")
    deadline = monotonic() + timeout
    while monotonic() < deadline:
        remaining = deadline - monotonic()
        try:
            result = run([systemctl, "show", "--property=Id", "--property=ActiveState", "--property=MainPID",
                          "--property=ControlGroup", "--property=Result", unit], stdin=subprocess.DEVNULL,
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
                         check=False, timeout=max(0.1, min(3, remaining)))
            if result.returncode == 0:
                props = _unit_properties(result.stdout)
                expected_id = unit if unit.endswith(".service") else unit + ".service"
                control_group = props.get("ControlGroup", "")
                expected_cgroup = "/system.slice/" + expected_id
                cgroup = _cgroup_state(control_group, read_events) if control_group else CgroupState.ABSENT
                if (props.get("Id") == expected_id and "ControlGroup" in props
                        and (control_group in (expected_cgroup, ""))
                        and props.get("ActiveState") in ("inactive", "failed")
                        and props.get("MainPID") == "0"
                        and (cgroup == CgroupState.EMPTY
                             or (allow_removed_after_success and props.get("Result") == "success"
                                 and cgroup == CgroupState.ABSENT))):
                    return True
        except (OSError, subprocess.TimeoutExpired, ValueError):
            pass
        sleep(min(0.25, max(0, deadline - monotonic())))
    return False


def stop_and_confirm_unit(systemctl, unit, run=subprocess.run, monotonic=time.monotonic,
                          sleep=time.sleep, read_events=None, grace_timeout=5, kill_timeout=15):
    """Request graceful stop, then escalate and inspect within finite deadlines."""
    try:
        run([systemctl, "stop", "--no-block", unit],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            check=False, timeout=3)
    except (OSError, subprocess.TimeoutExpired, KeyboardInterrupt):
        pass
    if confirm_unit_terminated(systemctl, unit, run, monotonic, sleep, read_events, grace_timeout):
        return True
    try:
        run([systemctl, "kill", "--kill-whom=all", "--signal=SIGKILL", unit],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            check=False, timeout=3)
    except (OSError, subprocess.TimeoutExpired, KeyboardInterrupt):
        pass
    return confirm_unit_terminated(systemctl, unit, run, monotonic, sleep, read_events, kill_timeout)


def execute_unit(command, unit, systemctl, run=subprocess.run, monotonic=time.monotonic,
                 sleep=time.sleep, read_events=None, timeout=45, stop_grace=5, kill_timeout=15):
    """Execute the manager, then boundedly confirm its transient unit is empty."""
    timed_out = interrupted = False
    error = ""
    returncode = 1
    try:
        result = run(command, stdin=subprocess.DEVNULL, check=False, timeout=timeout, text=True)
        returncode = result.returncode
    except subprocess.TimeoutExpired:
        timed_out = True
        returncode = 124
    except KeyboardInterrupt:
        interrupted = True
        returncode = 130
    except Exception as exc:
        error = str(exc)
    try:
        if timed_out or interrupted or error:
            terminated = stop_and_confirm_unit(systemctl, unit, run, monotonic, sleep, read_events,
                                               stop_grace, kill_timeout)
        else:
            try:
                terminated = confirm_unit_terminated(systemctl, unit, run, monotonic, sleep,
                                                     read_events, kill_timeout,
                                                     allow_removed_after_success=(returncode == 0))
            except KeyboardInterrupt:
                interrupted = True
                returncode = 130
                terminated = stop_and_confirm_unit(systemctl, unit, run, monotonic, sleep,
                                                   read_events, stop_grace, kill_timeout)
            else:
                if not terminated:
                    terminated = stop_and_confirm_unit(systemctl, unit, run, monotonic, sleep,
                                                       read_events, stop_grace, kill_timeout)
    except KeyboardInterrupt:
        # A repeated interrupt can interrupt the bounded stop/confirmation path.
        # The stop request may have been sent, but termination is then unknown.
        interrupted = True
        returncode = 130
        terminated = False
    return UnitOutcome(returncode, terminated, timed_out, interrupted, error)


def main():
    if len(sys.argv) > 2 or (len(sys.argv) == 2 and sys.argv[1] != "--record"):
        print("Usage: athena-alpha-m1-systemd-canary.py [--record]", file=sys.stderr)
        return 2
    if os.geteuid() != 0:
        print("This canary is for an administrator on the isolated VM.", file=sys.stderr)
        return 2
    if (SYSTEMD_RUN is None or SYSTEMCTL is None
            or not os.path.isfile(SYSTEMD_RUN) or not os.path.isfile(SYSTEMCTL)):
        print("systemd-run/systemctl unavailable", file=sys.stderr)
        return 2
    try:
        ensure_receipt_safe(Path(RECEIPT))
    except Exception as exc:
        print(f"Cannot safely preserve canary receipt state: {exc}", file=sys.stderr)
        return 2
    nonce = secrets.token_hex(12)
    try:
        host_mnt = str(os.stat("/proc/self/ns/mnt").st_ino)
        host_net = str(os.stat("/proc/self/ns/net").st_ino)
        host_uts = str(os.stat("/proc/self/ns/uts").st_ino)
        host_dev_mount = None
        with open("/proc/self/mountinfo", encoding="ascii") as stream:
            for line in stream:
                fields = line.split()
                if len(fields) > 4 and fields[4] == "/dev":
                    host_dev_mount = fields[0]
                    break
    except Exception as exc:
        print(f"Cannot prepare namespace checks: {exc}", file=sys.stderr)
        return 2
    if host_dev_mount is None:
        print("Cannot identify the host /dev mount", file=sys.stderr)
        return 2
    host_tmp = None
    host_var_tmp_marker = None
    unit = "athena-alpha-m1-canary-" + nonce
    result_code = 1
    outcome = None
    preserve_markers = False
    command = [SYSTEMD_RUN, "--quiet", "--wait", "--pipe", "--unit=" + unit,
               "--property=Type=exec", "--property=RuntimeMaxSec=30s",
               "--property=KillMode=control-group", "--property=ProtectSystem=strict",
               "--property=ProtectHome=yes", "--property=PrivateTmp=yes",
               "--property=PrivateDevices=yes", "--property=PrivateMounts=yes",
               "--property=PrivateNetwork=yes", "--property=RestrictAddressFamilies=AF_UNIX",
               "--property=ProtectKernelTunables=yes", "--property=ProtectKernelModules=yes",
               "--property=ProtectControlGroups=yes", "--property=ProtectKernelLogs=yes",
               "--property=ProtectHostname=yes", "--property=ProtectClock=yes",
               "--property=ProtectProc=invisible",
               "--property=InaccessiblePaths=/run/dbus /run/systemd/private /run/systemd/system",
               "--property=TasksMax=128",
               "--property=MemoryMax=4G", "--property=CPUQuota=200%",
               "--property=LockPersonality=yes", "--property=NoNewPrivileges=yes",
               "--property=CapabilityBoundingSet=CAP_SYS_ADMIN CAP_CHOWN",
               "--property=ReadWritePaths=/tmp " + EXT4_PARENT,
               "--property=ReadOnlyPaths=" + BUILD + " " + LOGDIR +
               " /var/lib/athena-alpha-m1 /var/lib/athena-alpha-m1/backups",
               "--property=UMask=0077", "--setenv=PATH=/usr/sbin:/usr/bin:/sbin:/bin",
               "--setenv=HOME=/tmp", "--setenv=LANG=C.UTF-8", "--setenv=LC_ALL=C.UTF-8",
               "--setenv=TMPDIR=/tmp"]
    try:
        host_tmp = tempfile.mkdtemp(prefix="athena-alpha-m1-canary-", dir="/tmp")
        marker = os.path.join(host_tmp, "host-marker")
        with open(marker, "x", encoding="ascii") as f:
            f.write("host-only\n")
        marker_fd, host_var_tmp_marker = tempfile.mkstemp(prefix="athena-alpha-m1-canary-", suffix="-host-marker", dir="/var/tmp")
        with os.fdopen(marker_fd, "w", encoding="ascii") as f:
            f.write("host-only\n")
        command.extend(["--", PYTHON, "-c", CHILD,
                        nonce, host_tmp, host_var_tmp_marker, host_mnt, host_net, host_uts, host_dev_mount,
                        EXT4_PARENT])
        outcome = execute_unit(command, unit, SYSTEMCTL)
        preserve_markers = outcome.preserve_markers
        if outcome.timed_out:
            print("Canary timed out; transient unit termination confirmation required", file=sys.stderr)
            result_code = 124
        elif outcome.interrupted:
            print("Canary interrupted; transient unit termination confirmation required", file=sys.stderr)
            result_code = 130
        elif outcome.error:
            print(f"Canary execution failed: {outcome.error}", file=sys.stderr)
            result_code = 1
        elif not outcome.terminated:
            print("Canary termination is uncertain; preserving markers and receipt state", file=sys.stderr)
            result_code = 1
        elif outcome.returncode != 0:
            print(f"Canary failed: systemd-run exit {outcome.returncode}", file=sys.stderr)
            result_code = outcome.returncode or 1
        elif (not os.path.isfile(marker) or os.path.exists(os.path.join("/tmp", nonce + ".private"))
              or not os.path.isfile(host_var_tmp_marker)
              or os.path.exists(os.path.join("/var/tmp", nonce + ".private"))):
            print("Canary failed: host /tmp or /var/tmp changed unexpectedly", file=sys.stderr)
            result_code = 1
        else:
            print("PASS: host /tmp and /var/tmp remained isolated")
            if len(sys.argv) == 2:
                record_success(outcome)
                print("PASS: root-owned canary receipt recorded")
            result_code = 0
    except Exception as exc:
        print(f"Canary preparation/execution failed: {exc}", file=sys.stderr)
        result_code = 1
    except KeyboardInterrupt:
        preserve_markers = True
        print("Canary interrupted; preserving markers and receipt state", file=sys.stderr)
        result_code = 130
    finally:
        if outcome is not None and outcome.terminated:
            try:
                subprocess.run([SYSTEMCTL, "reset-failed", unit], check=False,
                               stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=5)
            except Exception as exc:
                print(f"Warning: could not clear transient unit state: {exc}", file=sys.stderr)
        can_cleanup = cleanup_allowed(outcome, preserve_markers)
        if not can_cleanup:
            print("Canary markers retained for inspection", file=sys.stderr)
        elif host_tmp is not None:
            try:
                shutil.rmtree(host_tmp)
            except FileNotFoundError:
                pass
            except Exception as exc:
                print(f"Warning: could not remove canary temporary directory: {exc}", file=sys.stderr)
                if result_code == 0:
                    result_code = 1
        if can_cleanup and host_var_tmp_marker is not None:
            try:
                os.unlink(host_var_tmp_marker)
            except FileNotFoundError:
                pass
            except Exception as exc:
                print(f"Warning: could not remove /var/tmp canary marker: {exc}", file=sys.stderr)
                if result_code == 0:
                    result_code = 1
    return result_code


if __name__ == "__main__":
    raise SystemExit(main())
