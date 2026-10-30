#!/usr/bin/python3
"""Root entry point for one allowlisted Athena Alpha M1 qualification case.

This is an installation candidate, not a general-purpose sandbox. It requires
systemd-run and must only be enabled on a disposable qualification VM.
"""

import hashlib
import json
import os
import secrets
import signal
import stat
import subprocess
import sys
import time
import fcntl
from pathlib import Path

from gateway_support import (GatewayError, authorize_case, remove_staged_artifacts,
                             stage_artifacts, wait_for_unit_termination,
                             terminate_client_process, write_evidence_manifest)

BUILD = Path("/opt/athena-qualification/build")
LOGDIR = Path("/var/log/athena-alpha-m1")
EXT4_PARENT = Path("/var/lib/athena-alpha-m1/ext4-fixture")
SYSTEMD_RUN = "/usr/bin/systemd-run"
SYSTEMCTL = "/usr/bin/systemctl"
TIMEOUT_SECONDS = 180
CLIENT_GRACE_SECONDS = 15
MAX_LOG_BYTES = 16 * 1024 * 1024
STAGING_ROOT = Path("/run/athena-alpha-m1/staging")
RUNNER_LOCK = Path("/run/athena-alpha-m1/runner.lock")
FIXTURE_LOCK = Path("/run/athena-alpha-m1/fixture.lock")
FLOCK = "/usr/bin/flock"
STOP_GRACE_SECONDS = 20
SUPPORT_SHA256 = "92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514"

PINNED_SHA256 = {
    "activation_execution_privileged_test": "d87a84cbb1e18ba073beaadb1bb7737b61fa448081af37e4d4c5a6c29337d157",
    "activation_plan_retention_test": "f76251d6e0af0bd892d1dcb17f41f80b13343c7c274e289014dfcb090370e474",
    "activation_plan_persistence_test": "f12620d89bb55e66f1e1960dd8852f2fdf27c9a148967d222d562fdddbd5aaa3",
    "legacy_d3_test": "61e23e8c9cd3a81c8766eb61e93e01cc7c9af2025a80f6056f28d3ac137ad6a9",
    "initial_activation_test": "934222dc181ae1a98a3d00bed5cba8e208cdad194d0c10fe552b1e5cf7e74243",
    "libathena_core.so": "e212420cb34aeca76cebe2ecf396832f25c3c4f119b846b42ef00d2994cc995a",
}

# CTest names and exact executable arguments from the authenticated CTest file.
ALLOWLIST = {
    "activation_execution_privileged_sources_test": ("activation_execution_privileged_test", "sources"),
    "activation_execution_privileged_legacy_test": ("activation_execution_privileged_test", "legacy"),
    "activation_execution_privileged_owner_test": ("activation_execution_privileged_test", "owner"),
    "activation_execution_privileged_bind_test": ("activation_execution_privileged_test", "bind"),
    "activation_execution_privileged_nested_test": ("activation_execution_privileged_test", "nested"),
    "activation_execution_privileged_ext4_test": ("activation_execution_privileged_test", "ext4"),
    "activation_plan_retention_privileged_owner_test": ("activation_plan_retention_test", "privileged-owner"),
    "activation_plan_retention_privileged_bind_test": ("activation_plan_retention_test", "privileged-bind"),
    "activation_plan_retention_privileged_nested_test": ("activation_plan_retention_test", "privileged-nested"),
    "activation_plan_retention_privileged_ext4_test": ("activation_plan_retention_test", "privileged-ext4"),
    "activation_plan_persistence_privileged_test": ("activation_plan_persistence_test", "privileged"),
    "activation_plan_persistence_privileged_owner_test": ("activation_plan_persistence_test", "privileged-owner"),
    "activation_plan_persistence_privileged_bind_test": ("activation_plan_persistence_test", "privileged-bind"),
    "activation_plan_persistence_privileged_nested_test": ("activation_plan_persistence_test", "privileged-nested"),
    "activation_plan_persistence_privileged_tmpfs_test": ("activation_plan_persistence_test", "privileged-tmpfs"),
    "activation_plan_persistence_privileged_ext4_test": ("activation_plan_persistence_test", "privileged-ext4"),
    "legacy_d3_scanner_test": ("legacy_d3_test", "scanner"),
    "initial_activation_migration_test": ("initial_activation_test", "migration"),
}


def fail(message):
    print(message, file=sys.stderr)
    raise SystemExit(2)


def hash_file(path, executable=False):
    digest = hashlib.sha256()
    flags = os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW
    fd = os.open(path, flags)
    try:
        st = os.fstat(fd)
        if (not stat.S_ISREG(st.st_mode) or st.st_uid not in (0, 65534)
                or st.st_mode & 0o022 or (st.st_uid != 0 and st.st_mode & 0o200)
                or st.st_mode & 0o7000 or (executable and not st.st_mode & 0o111)):
            raise RuntimeError(f"unsafe protected artifact metadata: {path}")
        reject_acl(fd, path)
        while True:
            block = os.read(fd, 1024 * 1024)
            if not block:
                break
            digest.update(block)
    finally:
        os.close(fd)
    return digest.hexdigest()


def reject_acl(fd, path):
    try:
        names = os.listxattr(fd)
    except OSError as exc:
        raise RuntimeError(f"cannot inspect ACLs: {path}: {exc}") from exc
    if "system.posix_acl_default" in names:
        raise RuntimeError(f"default POSIX ACL is not permitted: {path}")
    # Access ACLs are allowed only when their kernel-effective mask is safe:
    # the mode bits checked by the caller encode the ACL mask for group class.


def checked_directory(path, exact_mode=None, allowed_owners=(0,)):
    path = Path(path)
    if not path.is_absolute() or any(part in (".", "..") for part in path.parts):
        raise RuntimeError(f"directory path must be absolute and normalized: {path}")
    fd = os.open("/", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    walked = Path("/")
    try:
        for part in path.parts[1:]:
            child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                            dir_fd=fd)
            os.close(fd)
            fd = child
            walked = walked / part
            st = os.fstat(fd)
            mode = stat.S_IMODE(st.st_mode)
            if not stat.S_ISDIR(st.st_mode) or st.st_uid not in allowed_owners:
                raise RuntimeError(f"unsafe directory owner/type: {walked}")
            if st.st_uid != 0 and mode & 0o200:
                raise RuntimeError(f"non-root-owned protected directory is owner-writable: {walked}")
            if mode & 0o7000:
                raise RuntimeError(f"special directory mode bits are not permitted: {walked}")
            if mode & 0o022:
                raise RuntimeError(f"directory is group/other writable: {walked}")
            reject_acl(fd, walked)
        final_mode = stat.S_IMODE(os.fstat(fd).st_mode)
        if exact_mode is not None and final_mode != exact_mode:
            raise RuntimeError(f"directory mode must be {exact_mode:04o}: {path}")
        return fd
    except Exception:
        os.close(fd)
        raise


def checked_root_dir(path, exact_mode=None):
    return checked_directory(path, exact_mode=exact_mode)


def checked_build_dir():
    return checked_directory(BUILD, allowed_owners=(0, 65534))


def validate_ext4_fixture():
    fd = checked_root_dir(EXT4_PARENT, 0o700)
    try:
        if os.listdir(fd):
            raise RuntimeError("ext4 fixture directory is not empty")
    finally:
        os.close(fd)
    result = subprocess.run(
        ["/usr/bin/findmnt", "-n", "-T", str(EXT4_PARENT), "-o", "FSTYPE"],
        check=True, text=True, capture_output=True,
        env={"PATH": "/usr/bin:/bin", "LC_ALL": "C"}, timeout=5,
    )
    if result.stdout.strip() != "ext4":
        raise RuntimeError("ext4 fixture path is not on ext4")


def check_ext4_fixture_empty_after_run():
    fd = checked_root_dir(EXT4_PARENT, 0o700)
    try:
        if os.listdir(fd):
            raise RuntimeError("ext4 fixture not empty after test; preserve it for investigation")
    finally:
        os.close(fd)


def create_runtime_layout():
    run_fd = checked_root_dir(Path("/run"))
    try:
        try:
            os.mkdir("athena-alpha-m1", 0o700, dir_fd=run_fd)
        except FileExistsError:
            pass
        base_fd = os.open("athena-alpha-m1", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                          dir_fd=run_fd)
    finally:
        os.close(run_fd)
    try:
        st = os.fstat(base_fd)
        if st.st_uid != 0 or st.st_gid != 0 or stat.S_IMODE(st.st_mode) != 0o700:
            raise RuntimeError("unsafe /run/athena-alpha-m1 directory")
        reject_acl(base_fd, "/run/athena-alpha-m1")
        try:
            os.mkdir("staging", 0o700, dir_fd=base_fd)
        except FileExistsError:
            pass
        stage_fd = os.open("staging", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                           dir_fd=base_fd)
        stage_st = os.fstat(stage_fd)
        if stage_st.st_uid != 0 or stage_st.st_gid != 0 or stat.S_IMODE(stage_st.st_mode) != 0o700:
            os.close(stage_fd)
            raise RuntimeError("unsafe Alpha M1 runtime staging directory")
        reject_acl(stage_fd, "/run/athena-alpha-m1/staging")
        return base_fd, stage_fd
    except Exception:
        os.close(base_fd)
        raise


def open_lock(path):
    parent_fd = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW)
    try:
        fd = os.open(path.name, os.O_RDWR | os.O_CREAT | os.O_CLOEXEC | os.O_NOFOLLOW,
                     0o600, dir_fd=parent_fd)
    finally:
        os.close(parent_fd)
    st = os.fstat(fd)
    if not stat.S_ISREG(st.st_mode) or st.st_uid != 0 or st.st_nlink != 1 or stat.S_IMODE(st.st_mode) != 0o600:
        os.close(fd)
        raise RuntimeError(f"unsafe lock file: {path}")
    reject_acl(fd, path)
    return fd


def systemd_unit_state(unit):
    result = subprocess.run(
        [SYSTEMCTL, "show", "--no-pager", "--property=Id", "--property=ActiveState", "--property=MainPID",
         "--property=ControlGroup", "--property=Result", unit],
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, timeout=5, check=False, env={"PATH": "/usr/bin:/bin", "LC_ALL": "C"})
    if result.returncode != 0:
        raise RuntimeError(f"cannot inspect systemd unit: {result.stderr.strip()}")
    fields = {}
    for line in result.stdout.splitlines():
        key, separator, value = line.partition("=")
        if separator:
            fields[key] = value
    active = fields.get("ActiveState")
    unit_id = fields.get("Id")
    pid_text = fields.get("MainPID")
    control_group = fields.get("ControlGroup")
    expected_id = unit if unit.endswith(".service") else unit + ".service"
    if unit_id != expected_id:
        raise RuntimeError("systemd returned an unexpected transient unit identity")
    if active not in ("active", "activating", "deactivating", "reloading", "inactive", "failed"):
        raise RuntimeError("unexpected systemd ActiveState")
    if not pid_text or not pid_text.isdecimal() or control_group is None:
        raise RuntimeError("incomplete systemd unit state")
    expected_cgroup = "/system.slice/" + expected_id
    if control_group not in ("", expected_cgroup):
        raise RuntimeError("systemd returned an unexpected transient unit cgroup")
    populated = cgroup_populated(control_group)
    return {"unit_id": unit_id, "active_state": active, "main_pid": int(pid_text),
            "cgroup_populated": populated,
            "result": fields.get("Result", "unknown"),
            "control_group": control_group}


def cgroup_populated(control_group):
    if control_group == "":
        return None
    candidate = Path(control_group)
    if (not candidate.is_absolute() or ".." in candidate.parts or "." in candidate.parts
            or len(candidate.parts) < 3 or candidate.parts[1] != "system.slice"
            or not candidate.parts[2].startswith("athena-alpha-m1-")):
        raise RuntimeError("unexpected systemd control group path")
    fd = os.open("/sys/fs/cgroup", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW)
    try:
        try:
            for part in candidate.parts[1:]:
                child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=fd)
                os.close(fd)
                fd = child
        except FileNotFoundError:
            return None
        try:
            events_fd = os.open("cgroup.events", os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=fd)
        except FileNotFoundError:
            return None
        try:
            events = os.read(events_fd, 4096).decode("ascii")
        finally:
            os.close(events_fd)
    finally:
        os.close(fd)
    for line in events.splitlines():
        key, _, value = line.partition(" ")
        if key == "populated" and value in ("0", "1"):
            return value == "1"
    raise RuntimeError("cgroup.events has no valid populated field")


def stop_systemd_unit(unit):
    result = subprocess.run([SYSTEMCTL, "stop", "--no-block", unit],
                            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                            stderr=subprocess.PIPE, text=True, timeout=5, check=False,
                            env={"PATH": "/usr/bin:/bin", "LC_ALL": "C"})
    if result.returncode != 0:
        raise RuntimeError(f"systemctl stop failed: {result.stderr.strip()}")


def request_stop(_signum, _frame):
    global stop_requested
    stop_requested = True


stop_requested = False


def write_all(fd, data):
    view = memoryview(data)
    while view:
        written = os.write(fd, view)
        if written <= 0:
            raise OSError("short write to audit log")
        view = view[written:]


def main():
    if os.geteuid() != 0:
        fail("runner must be invoked through its restricted sudo rule")
    if len(sys.argv) != 2:
        fail("usage: athena-alpha-m1-runner CTEST_CASE")
    try:
        exe_name, mode = authorize_case(sys.argv[1], ALLOWLIST)
    except GatewayError:
        fail("usage: athena-alpha-m1-runner CTEST_CASE")
    case = sys.argv[1]
    support_path = Path(__file__).resolve().with_name("gateway_support.py")
    if hashlib.sha256(support_path.read_bytes()).hexdigest() != SUPPORT_SHA256:
        fail("gateway support module hash mismatch")
    for path in (SYSTEMD_RUN, SYSTEMCTL, FLOCK):
        try:
            parent_fd = checked_root_dir(Path(path).parent)
            fd = os.open(Path(path).name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=parent_fd)
            try:
                metadata = os.fstat(fd)
                if (not stat.S_ISREG(metadata.st_mode) or metadata.st_uid != 0
                        or metadata.st_mode & 0o022 or not metadata.st_mode & 0o111):
                    raise RuntimeError(f"unsafe system executable: {path}")
                reject_acl(fd, path)
            finally:
                os.close(fd)
                os.close(parent_fd)
        except Exception as exc:
            fail(f"required protected system executable unavailable: {path}: {exc}")

    ext4_env = None
    if case.endswith("_ext4_test"):
        ext4_env = ("ATHENA_D67_EXT4_TMPDIR" if case.startswith("activation_execution_")
                    else "ATHENA_D5_EXT4_TMPDIR" if case.startswith("activation_plan_retention_")
                    else "ATHENA_D4_EXT4_TMPDIR")
    elif case == "activation_plan_persistence_privileged_test":
        ext4_env = "ATHENA_D4_EXT4_TMPDIR"

    logdir_fd = checked_root_dir(LOGDIR, 0o700)
    run_id = f"{time.time_ns()}-{secrets.token_hex(8)}-{case}"
    log_name, output_name = run_id + ".json", run_id + ".out"
    log_fd = os.open(log_name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
                     0o600, dir_fd=logdir_fd)
    output_fd = os.open(output_name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
                        0o600, dir_fd=logdir_fd)
    started = time.time_ns()
    unit = "athena-alpha-m1-" + secrets.token_hex(12)
    status, timed_out, stop_was_requested, termination_confirmed = 125, False, False, False
    client_terminated = True
    classification, error, unit_state, stage_manifest = "INFRA_ERROR", None, None, None
    process = None
    selector = None
    stage_parent_fd = base_fd = build_fd = run_lock_fd = fixture_lock_fd = None
    stage_id = None
    stream_hash = hashlib.sha256()
    saved_hash = hashlib.sha256()
    stream_bytes = saved_bytes = 0
    old_term = signal.signal(signal.SIGTERM, request_stop)
    old_int = signal.signal(signal.SIGINT, request_stop)
    try:
        base_fd, stage_parent_fd = create_runtime_layout()
        run_lock_fd = os.open("runner.lock", os.O_RDWR | os.O_CREAT | os.O_CLOEXEC | os.O_NOFOLLOW,
                              0o600, dir_fd=base_fd)
        run_lock_st = os.fstat(run_lock_fd)
        if (not stat.S_ISREG(run_lock_st.st_mode) or run_lock_st.st_uid != 0 or run_lock_st.st_gid != 0
                or run_lock_st.st_nlink != 1
                or stat.S_IMODE(run_lock_st.st_mode) != 0o600):
            raise RuntimeError("unsafe runner serialization lock")
        reject_acl(run_lock_fd, RUNNER_LOCK)
        try:
            fcntl.flock(run_lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise RuntimeError("another gateway operation is active") from exc
        fixture_lock_fd = os.open("fixture.lock", os.O_RDWR | os.O_CREAT | os.O_CLOEXEC | os.O_NOFOLLOW,
                                  0o600, dir_fd=base_fd)
        fixture_st = os.fstat(fixture_lock_fd)
        if (not stat.S_ISREG(fixture_st.st_mode) or fixture_st.st_uid != 0 or fixture_st.st_gid != 0
                or fixture_st.st_nlink != 1
                or stat.S_IMODE(fixture_st.st_mode) != 0o600):
            raise RuntimeError("unsafe fixture lock")
        reject_acl(fixture_lock_fd, FIXTURE_LOCK)
        try:
            fcntl.flock(fixture_lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise RuntimeError("Alpha M1 fixture is held by a previous or concurrent unit") from exc
        fcntl.flock(fixture_lock_fd, fcntl.LOCK_UN)

        build_fd = checked_build_dir()
        stage_id, stage_manifest = stage_artifacts(
            build_fd, stage_parent_fd,
            {exe_name: (PINNED_SHA256[exe_name], True),
             "libathena_core.so": (PINNED_SHA256["libathena_core.so"], False)},
            run_id, source_owner=(0, 65534), stage_owner=0)
        executable = STAGING_ROOT / stage_id / exe_name
        staged_library_dir = str(STAGING_ROOT / stage_id)
        if ext4_env:
            validate_ext4_fixture()

        env = {"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "HOME": "/tmp",
               "LANG": "C.UTF-8", "LC_ALL": "C.UTF-8", "TMPDIR": "/tmp",
               "LD_LIBRARY_PATH": staged_library_dir}
        if ext4_env:
            env[ext4_env] = str(EXT4_PARENT)
        rw_paths = ["/tmp"]
        if ext4_env:
            rw_paths.append(str(EXT4_PARENT))
        fixture_lock_path = str(RUNNER_LOCK.parent / "fixture.lock")
        rw_paths.append(fixture_lock_path)
        command = [SYSTEMD_RUN, "--quiet", "--wait", "--pipe",
                   "--unit=" + unit, "--property=Type=exec",
                   f"--property=RuntimeMaxSec={TIMEOUT_SECONDS + 30}s",
                   "--property=KillMode=control-group", "--property=TimeoutStopSec=5s",
                   "--property=ProtectSystem=strict", "--property=ProtectHome=yes",
                   "--property=PrivateTmp=yes", "--property=PrivateDevices=yes",
                   "--property=PrivateMounts=yes", "--property=PrivateNetwork=yes",
                   "--property=RestrictAddressFamilies=AF_UNIX",
                   "--property=ProtectKernelTunables=yes",
                   "--property=ProtectKernelModules=yes", "--property=ProtectControlGroups=yes",
                   "--property=ProtectKernelLogs=yes", "--property=ProtectHostname=yes",
                   "--property=ProtectClock=yes", "--property=ProtectProc=invisible",
                   "--property=InaccessiblePaths=/run/dbus /run/systemd/private /run/systemd/system",
                   "--property=TasksMax=128", "--property=MemoryMax=4G",
                   "--property=CPUQuota=200%",
                   "--property=LockPersonality=yes", "--property=NoNewPrivileges=yes",
                   "--property=CapabilityBoundingSet=CAP_SYS_ADMIN CAP_CHOWN",
                   "--property=ReadWritePaths=" + " ".join(rw_paths),
                   "--property=ReadOnlyPaths=/opt/athena-qualification /var/log/athena-alpha-m1 " + staged_library_dir,
                   "--property=UMask=0077"]
        for name, value in env.items():
            command.append("--setenv=" + name + "=" + value)
        if stop_requested:
            stop_was_requested = True
            classification, status = "STOP_REQUESTED", 125
            termination_confirmed = True
            unit_state = {"active_state": "not-started", "main_pid": 0, "cgroup_populated": False}
            raise RuntimeError("stop requested before systemd unit launch")
        command.extend(["--", FLOCK, "--exclusive", "--nonblock", fixture_lock_path,
                        "--", str(executable), mode])
        process = subprocess.Popen(command, cwd=staged_library_dir, env={"PATH": "/usr/bin:/bin", "LC_ALL": "C"}, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   close_fds=True, start_new_session=True)
        client_terminated = False
        deadline = time.monotonic() + TIMEOUT_SECONDS + CLIENT_GRACE_SECONDS
        import selectors
        selector = selectors.DefaultSelector()
        selector.register(process.stdout, selectors.EVENT_READ)
        stop_deadline = None
        while selector.get_map() or process.poll() is None:
            now = time.monotonic()
            if stop_requested and not stop_was_requested:
                stop_was_requested = True
                stop_deadline = now + STOP_GRACE_SECONDS
                stop_systemd_unit(unit)
            elif now >= deadline and not stop_was_requested:
                timed_out = True
                stop_deadline = now + STOP_GRACE_SECONDS
                stop_systemd_unit(unit)
                stop_was_requested = True
            if stop_deadline is not None and now >= stop_deadline and process.poll() is None:
                process.kill()
                selector.close()
                break
            until = stop_deadline if stop_deadline is not None else deadline
            events = selector.select(timeout=min(0.5, max(0, until - time.monotonic()))) if selector.get_map() else []
            if not selector.get_map() and process.poll() is None:
                time.sleep(0.05)
            for key, _ in events:
                block = os.read(key.fd, 65536)
                if not block:
                    selector.unregister(key.fileobj)
                    continue
                stream_hash.update(block)
                stream_bytes += len(block)
                remaining = max(0, MAX_LOG_BYTES - saved_bytes)
                saved = block[:remaining]
                if saved:
                    write_all(output_fd, saved)
                    saved_hash.update(saved)
                    saved_bytes += len(saved)
        try:
            status = process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            status = process.wait(timeout=5)
        client_terminated = True
        if status < 0:
            status = 128 + abs(status)
        if process is not None:
            proof = wait_for_unit_termination(
                lambda: systemd_unit_state(unit),
                lambda: stop_systemd_unit(unit),
                deadline=time.monotonic() + STOP_GRACE_SECONDS,
                clock=time.monotonic, sleep=time.sleep,
                allow_removed_after_success=(status == 0 and not timed_out and not stop_was_requested))
            termination_confirmed = proof["termination_confirmed"]
            unit_state = proof["state"]
            if unit_state.get("result") == "timeout":
                timed_out = True
        else:
            termination_confirmed = True
            unit_state = {"active_state": "not-started", "main_pid": 0, "cgroup_populated": False}
        if not termination_confirmed:
            raise RuntimeError("unit termination is uncertain")
        if status == 0 and not timed_out and not stop_was_requested:
            classification = "PASS"
        elif timed_out:
            classification, status = "TIMEOUT", 124
        elif stop_was_requested:
            classification, status = "STOP_REQUESTED", 125
        else:
            classification = "TEST_FAIL"
        if ext4_env:
            check_ext4_fixture_empty_after_run()
    except Exception as exc:
        error = f"{type(exc).__name__}: {exc}"
        if process is None:
            termination_confirmed = True
            unit_state = {"active_state": "not-started", "main_pid": 0, "cgroup_populated": False}
        if process is not None and not termination_confirmed:
            try:
                stop_systemd_unit(unit)
            except Exception as stop_exc:
                error += f"; stop request failed: {type(stop_exc).__name__}: {stop_exc}"
            try:
                proof = wait_for_unit_termination(
                    lambda: systemd_unit_state(unit), lambda: None,
                    deadline=time.monotonic() + STOP_GRACE_SECONDS,
                    clock=time.monotonic, sleep=time.sleep)
                termination_confirmed = proof["termination_confirmed"]
                unit_state = proof["state"]
            except Exception as confirm_exc:
                termination_confirmed = False
                unit_state = {"termination_confirmed": False}
                error += f"; termination unconfirmed: {type(confirm_exc).__name__}: {confirm_exc}"
            if not terminate_client_process(process, wait_timeout=2, kill_timeout=5):
                client_terminated = False
                termination_confirmed = False
                error += "; systemd-run client termination is unconfirmed"
            else:
                client_terminated = True
        status = 125
        if "stop requested before systemd unit launch" in error:
            classification = "STOP_REQUESTED"
        elif not termination_confirmed:
            classification = "TERMINATION_UNCERTAIN"
        elif timed_out:
            status, classification = 124, "TIMEOUT"
        elif stop_was_requested:
            classification = "STOP_REQUESTED"
        else:
            classification = "INFRA_ERROR"
        if ext4_env and termination_confirmed:
            try:
                check_ext4_fixture_empty_after_run()
            except Exception as fixture_exc:
                error += f"; fixture preserved for investigation: {type(fixture_exc).__name__}: {fixture_exc}"

    stage_cleanup = "not-created"
    if stage_id is not None:
        if termination_confirmed:
            try:
                remove_staged_artifacts(stage_parent_fd, stage_id)
                stage_cleanup = "removed-after-confirmed-termination"
            except Exception as cleanup_exc:
                stage_cleanup = "preserved-cleanup-failed"
                status, classification = 125, "INFRA_ERROR"
                error = f"{error + '; ' if error else ''}stage cleanup failed: {type(cleanup_exc).__name__}: {cleanup_exc}"
        else:
            stage_cleanup = "preserved-termination-uncertain"
    ended = time.time_ns()
    record = {
        "schema": 3, "run_id": run_id, "case": case, "executable": str(locals().get("executable", "")),
        "argument": mode, "unit": unit, "invoking_uid": _sudo_uid(),
        "uid": os.getuid(), "euid": os.geteuid(), "started_ns": started,
        "ended_ns": ended, "timeout_seconds": TIMEOUT_SECONDS, "timed_out": timed_out,
        "stop_requested": stop_was_requested, "termination_confirmed": termination_confirmed,
        "client_terminated": client_terminated,
        "classification": classification, "unit_state": unit_state,
        "stage_cleanup": stage_cleanup,
        "exit_status": status, "error": error,
        "staging_manifest": stage_manifest,
        "runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "gateway_support_sha256": SUPPORT_SHA256,
        "output_file": output_name, "output_bytes": saved_bytes,
        "output_sha256": saved_hash.hexdigest(), "stream_bytes": stream_bytes,
        "stream_sha256": stream_hash.hexdigest(), "output_truncated": stream_bytes > MAX_LOG_BYTES,
    }
    data = (json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n").encode()
    try:
        os.fsync(output_fd)
        write_all(log_fd, data)
        os.fsync(log_fd)
        evidence_manifest_name, evidence_manifest_sha256 = write_evidence_manifest(
            logdir_fd, log_name, output_name, run_id, stage_manifest)
        os.fsync(logdir_fd)
    except Exception as exc:
        status, classification = 125, "EVIDENCE_ERROR"
        error = f"{error + '; ' if error else ''}{type(exc).__name__}: {exc}"
        record["classification"] = classification
        record["exit_status"] = status
        record["error"] = error
        data = (json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n").encode()
        os.ftruncate(log_fd, 0)
        os.lseek(log_fd, 0, os.SEEK_SET)
        write_all(log_fd, data)
        os.fsync(log_fd)
        try:
            os.fsync(logdir_fd)
        except OSError:
            pass
    finally:
        if selector is not None:
            try:
                selector.close()
            except Exception:
                pass
        if process is not None and process.stdout is not None:
            try:
                process.stdout.close()
            except OSError:
                pass
        os.close(output_fd)
        os.close(log_fd)
        os.close(logdir_fd)
        for fd in (fixture_lock_fd, run_lock_fd, build_fd, stage_parent_fd, base_fd):
            if fd is not None:
                try:
                    os.close(fd)
                except OSError:
                    pass
        signal.signal(signal.SIGTERM, old_term)
        signal.signal(signal.SIGINT, old_int)
    manifest_summary = locals().get("evidence_manifest_name")
    manifest_digest = locals().get("evidence_manifest_sha256")
    print(f"case={case} status={status} classification={classification} timeout={timed_out} "
          f"termination_confirmed={termination_confirmed} error={str(error is not None).lower()} "
          f"log={LOGDIR / log_name} manifest={manifest_summary} manifest_sha256={manifest_digest}")
    return status


def _sudo_uid():
    value = os.environ.get("SUDO_UID", "")
    return int(value) if value.isdecimal() else None


if __name__ == "__main__":
    raise SystemExit(main())
