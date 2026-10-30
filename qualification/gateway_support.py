"""Small, testable primitives used by the Alpha M1 privileged runner.

This module is installed beside the runner as part of the same reviewed bundle.
It is not a standalone privilege boundary.
"""

import hashlib
import json
import os
import re
import secrets
import stat
import subprocess
import uuid
from pathlib import PurePosixPath


class GatewayError(RuntimeError):
    pass


def authorize_case(case, allowlist):
    if not isinstance(case, str) or case not in allowlist:
        raise GatewayError("administrative invocation is not allowlisted")
    return allowlist[case]


def _safe_leaf(name):
    return bool(re.fullmatch(r"[A-Za-z0-9_.+-]{1,128}", name)) and name not in (".", "..")


def _digest_fd(fd, destination_fd=None, chunk_size=1024 * 1024):
    digest = hashlib.sha256()
    total = 0
    while True:
        block = os.read(fd, chunk_size)
        if not block:
            break
        digest.update(block)
        total += len(block)
        if destination_fd is not None:
            view = memoryview(block)
            while view:
                count = os.write(destination_fd, view)
                if count <= 0:
                    raise OSError("short write while staging verified artifact")
                view = view[count:]
    return digest.hexdigest(), total


def _metadata(fd, name, expected_owner, executable):
    st = os.fstat(fd)
    accepted_owners = expected_owner if isinstance(expected_owner, (tuple, set, frozenset)) else (expected_owner,)
    if not stat.S_ISREG(st.st_mode) or st.st_uid not in accepted_owners:
        raise GatewayError(f"unexpected artifact type or owner: {name}")
    if st.st_mode & 0o7022 or st.st_mode & 0o7000:
        raise GatewayError(f"unsafe artifact permissions: {name}")
    if executable and not st.st_mode & 0o111:
        raise GatewayError(f"artifact is not executable: {name}")
    try:
        if "system.posix_acl_default" in os.listxattr(fd):
            raise GatewayError(f"default ACL is not permitted: {name}")
    except OSError as exc:
        raise GatewayError(f"cannot inspect artifact ACL: {name}") from exc
    return st


def _stable_identity(before, after):
    fields = ("st_dev", "st_ino", "st_uid", "st_mode", "st_nlink", "st_size", "st_mtime_ns", "st_ctime_ns")
    return all(getattr(before, field) == getattr(after, field) for field in fields)


def stage_artifacts(source_dir_fd, stage_parent_fd, artifacts, run_id,
                    *, source_owner=(0, 65534), stage_owner=0, on_source_open=None):
    """Copy an exact artifact set from pinned source FDs into a private stage.

    `artifacts` maps leaf filename to (sha256, executable). The caller must hold
    verified directory FDs. A complete stage is published with one rename only
    after source and staged inode checks, digest verification, and fsync.
    """
    if not _safe_leaf(run_id) or not artifacts:
        raise GatewayError("invalid stage identity or empty artifact manifest")
    if any(not _safe_leaf(name) for name in artifacts):
        raise GatewayError("artifact names must be safe leaf names")
    temp_name = ".stage-" + secrets.token_hex(16)
    os.mkdir(temp_name, 0o700, dir_fd=stage_parent_fd)
    temp_fd = os.open(temp_name, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                      dir_fd=stage_parent_fd)
    complete = False
    opened = []
    manifest = {"schema": 1, "run_id": run_id, "artifacts": {}}
    try:
        if os.fstat(temp_fd).st_uid != stage_owner:
            raise GatewayError("staging directory owner mismatch")
        for name, (expected_hash, executable) in artifacts.items():
            if not re.fullmatch(r"[0-9a-f]{64}", expected_hash):
                raise GatewayError(f"invalid configured digest: {name}")
            source_fd = os.open(name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK,
                                dir_fd=source_dir_fd)
            opened.append(source_fd)
            before = _metadata(source_fd, name, source_owner, executable)
            if on_source_open is not None:
                on_source_open(name, source_fd)
            target_fd = os.open(name, os.O_RDWR | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
                                0o600, dir_fd=temp_fd)
            try:
                os.lseek(source_fd, 0, os.SEEK_SET)
                actual_hash, copied_bytes = _digest_fd(source_fd, target_fd)
                after = os.fstat(source_fd)
                if not _stable_identity(before, after):
                    raise GatewayError(f"source artifact changed while staging: {name}")
                if actual_hash != expected_hash:
                    raise GatewayError(f"source artifact hash mismatch: {name}")
                os.fchmod(target_fd, 0o500 if executable else 0o400)
                os.fsync(target_fd)
                os.lseek(target_fd, 0, os.SEEK_SET)
                staged_hash, staged_bytes = _digest_fd(target_fd)
                staged_st = os.fstat(target_fd)
                if staged_hash != expected_hash or staged_bytes != copied_bytes:
                    raise GatewayError(f"staged artifact verification failed: {name}")
                if staged_st.st_uid != stage_owner or staged_st.st_nlink != 1:
                    raise GatewayError(f"unsafe staged artifact identity: {name}")
                manifest["artifacts"][name] = {
                    "sha256": staged_hash, "size": staged_bytes,
                    "source_dev": before.st_dev, "source_ino": before.st_ino,
                    "source_uid": before.st_uid, "source_mode": stat.S_IMODE(before.st_mode),
                    "stage_dev": staged_st.st_dev, "stage_ino": staged_st.st_ino,
                    "stage_uid": staged_st.st_uid, "stage_mode": stat.S_IMODE(staged_st.st_mode),
                }
            finally:
                os.close(target_fd)
        manifest_data = (json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n").encode()
        manifest_fd = os.open("manifest.json", os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
                              0o400, dir_fd=temp_fd)
        try:
            view = memoryview(manifest_data)
            while view:
                n = os.write(manifest_fd, view)
                if n <= 0:
                    raise OSError("short manifest write")
                view = view[n:]
            os.fsync(manifest_fd)
        finally:
            os.close(manifest_fd)
        os.fchmod(temp_fd, 0o500)
        os.fsync(temp_fd)
        os.rename(temp_name, run_id, src_dir_fd=stage_parent_fd, dst_dir_fd=stage_parent_fd)
        os.fsync(stage_parent_fd)
        complete = True
        return run_id, manifest
    finally:
        for fd in opened:
            os.close(fd)
        os.close(temp_fd)
        if not complete:
            _remove_private_stage(stage_parent_fd, temp_name)


def _remove_private_stage(parent_fd, leaf):
    if not _safe_leaf(leaf):
        return
    try:
        fd = os.open(leaf, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                     dir_fd=parent_fd)
    except FileNotFoundError:
        return
    try:
        st = os.fstat(fd)
        if st.st_uid != os.geteuid() or st.st_nlink < 2:
            raise GatewayError("refusing to remove stage with unexpected identity")
        os.fchmod(fd, 0o700)
        for name in os.listdir(fd):
            if not _safe_leaf(name):
                raise GatewayError("unexpected entry in incomplete stage")
            item_fd = os.open(name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=fd)
            try:
                item_st = os.fstat(item_fd)
                if not stat.S_ISREG(item_st.st_mode) or item_st.st_uid != os.geteuid() or item_st.st_nlink != 1:
                    raise GatewayError("refusing to remove unexpected staged object")
            finally:
                os.close(item_fd)
            os.unlink(name, dir_fd=fd)
        os.fsync(fd)
    finally:
        os.close(fd)
    os.rmdir(leaf, dir_fd=parent_fd)
    os.fsync(parent_fd)


def remove_staged_artifacts(stage_parent_fd, run_id, expected_owner=0):
    if not _safe_leaf(run_id):
        raise GatewayError("invalid stage identity for cleanup")
    fd = os.open(run_id, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                 dir_fd=stage_parent_fd)
    try:
        if os.fstat(fd).st_uid != expected_owner:
            raise GatewayError("refusing to clean stage with unexpected owner")
        names = os.listdir(fd)
        if "manifest.json" not in names or not names:
            raise GatewayError("refusing to clean incomplete published stage")
        validated = []
        for name in names:
            if not _safe_leaf(name):
                raise GatewayError("unexpected entry in published stage")
            item_fd = os.open(name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=fd)
            try:
                item_st = os.fstat(item_fd)
                if not stat.S_ISREG(item_st.st_mode) or item_st.st_uid != expected_owner or item_st.st_nlink != 1:
                    raise GatewayError("refusing to remove unexpected staged object")
            finally:
                os.close(item_fd)
            validated.append(name)
        os.fchmod(fd, 0o700)
        for name in validated:
            os.unlink(name, dir_fd=fd)
        os.fsync(fd)
    finally:
        os.close(fd)
    os.rmdir(run_id, dir_fd=stage_parent_fd)
    os.fsync(stage_parent_fd)


def terminal_unit_state(state, *, allow_removed_after_success=False):
    """Confirm a stopped unit, allowing cgroup removal only after a successful wait."""
    cgroup_empty = state.get("cgroup_populated") is False
    cgroup_removed_after_success = (
        allow_removed_after_success
        and state.get("cgroup_populated") is None
        and state.get("result") == "success")
    return (state.get("active_state") in ("inactive", "failed")
            and state.get("main_pid") == 0
            and (cgroup_empty or cgroup_removed_after_success))


def wait_for_unit_termination(inspect, request_stop, *, deadline, clock, sleep,
                              poll_interval=0.1, allow_removed_after_success=False):
    """Bounded fail-closed wait; returns proof state or raises GatewayError."""
    stop_requested = False
    last_state = None
    while clock() < deadline:
        try:
            last_state = inspect()
        except Exception as exc:
            last_state = {"inspection_error": type(exc).__name__}
        if (isinstance(last_state, dict)
                and terminal_unit_state(last_state,
                                        allow_removed_after_success=allow_removed_after_success)):
            return {"termination_confirmed": True, "state": last_state,
                    "stop_requested": stop_requested}
        if not stop_requested:
            request_stop()
            stop_requested = True
        sleep(min(poll_interval, max(0.0, deadline - clock())))
    raise GatewayError(f"systemd unit termination is uncertain: {last_state!r}")


def terminate_client_process(process, *, wait_timeout, kill_timeout):
    """Reap the systemd-run client; a confirmed unit alone does not reap it."""
    try:
        process.wait(timeout=wait_timeout)
        return True
    except subprocess.TimeoutExpired:
        try:
            process.kill()
        except ProcessLookupError:
            pass
    try:
        process.wait(timeout=kill_timeout)
        return True
    except subprocess.TimeoutExpired:
        return False


def verify_report(directory_fd, report_name, output_name):
    """Recompute report/output integrity using directory-relative no-follow opens."""
    for leaf in (report_name, output_name):
        if not _safe_leaf(leaf):
            raise GatewayError("invalid evidence filename")
    opened = []
    try:
        for leaf in (report_name, output_name):
            fd = os.open(leaf, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=directory_fd)
            opened.append(fd)
            st = os.fstat(fd)
            if not stat.S_ISREG(st.st_mode) or st.st_nlink != 1:
                raise GatewayError("evidence is not a unique regular file")
        report_bytes = b""
        while True:
            block = os.read(opened[0], 65536)
            if not block:
                break
            report_bytes += block
        record = json.loads(report_bytes)
        output_hash = hashlib.sha256()
        output_size = 0
        while True:
            block = os.read(opened[1], 1024 * 1024)
            if not block:
                break
            output_hash.update(block)
            output_size += len(block)
        if record.get("output_sha256") != output_hash.hexdigest() or record.get("output_bytes") != output_size:
            raise GatewayError("report/output integrity mismatch")
        return {"report_sha256": hashlib.sha256(report_bytes).hexdigest(),
                "output_sha256": output_hash.hexdigest(), "output_bytes": output_size}
    finally:
        for fd in opened:
            os.close(fd)


def write_evidence_manifest(directory_fd, report_name, output_name, run_id, artifact_manifest=None):
    if not _safe_leaf(report_name) or not _safe_leaf(output_name) or not _safe_leaf(run_id):
        raise GatewayError("invalid evidence manifest identity")
    verified = verify_report(directory_fd, report_name, output_name)
    report_fd = os.open(report_name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=directory_fd)
    output_fd = os.open(output_name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=directory_fd)
    try:
        report_st, output_st = os.fstat(report_fd), os.fstat(output_fd)
        manifest = {
            "schema": 1, "run_id": run_id,
            "report": {"name": report_name, "sha256": verified["report_sha256"],
                       "size": report_st.st_size, "uid": report_st.st_uid,
                       "gid": report_st.st_gid, "mode": stat.S_IMODE(report_st.st_mode)},
            "output": {"name": output_name, "sha256": verified["output_sha256"],
                       "size": verified["output_bytes"], "uid": output_st.st_uid,
                       "gid": output_st.st_gid, "mode": stat.S_IMODE(output_st.st_mode)},
            "staging": artifact_manifest,
        }
    finally:
        os.close(report_fd)
        os.close(output_fd)
    name = report_name[:-5] + ".manifest.json" if report_name.endswith(".json") else report_name + ".manifest.json"
    raw = (json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n").encode()
    fd = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
                 0o600, dir_fd=directory_fd)
    try:
        view = memoryview(raw)
        while view:
            count = os.write(fd, view)
            if count <= 0:
                raise OSError("short evidence manifest write")
            view = view[count:]
        os.fsync(fd)
    finally:
        os.close(fd)
    os.fsync(directory_fd)
    return name, hashlib.sha256(raw).hexdigest()


def verify_evidence_manifest(directory_fd, manifest_name):
    if not _safe_leaf(manifest_name):
        raise GatewayError("invalid manifest filename")
    fd = os.open(manifest_name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=directory_fd)
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode) or st.st_nlink != 1:
            raise GatewayError("unsafe evidence manifest")
        raw = bytearray()
        while True:
            block = os.read(fd, 65536)
            if not block:
                break
            raw.extend(block)
        record = json.loads(raw)
    finally:
        os.close(fd)
    report_name = record.get("report", {}).get("name")
    output_name = record.get("output", {}).get("name")
    verified = verify_report(directory_fd, report_name, output_name)
    for key, actual in (("report", verified["report_sha256"]), ("output", verified["output_sha256"])):
        item = record.get(key, {})
        evidence_fd = os.open(item.get("name", ""), os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW,
                              dir_fd=directory_fd)
        try:
            evidence_st = os.fstat(evidence_fd)
        finally:
            os.close(evidence_fd)
        if (item.get("sha256") != actual or item.get("size") != evidence_st.st_size
                or item.get("uid") != evidence_st.st_uid or item.get("gid") != evidence_st.st_gid
                or item.get("mode") != stat.S_IMODE(evidence_st.st_mode)):
            raise GatewayError(f"evidence manifest mismatch: {key}")
    return {"manifest_sha256": hashlib.sha256(raw).hexdigest(), **verified}


def validate_backup_manifest(directory_fd, manifest_name="manifest.json"):
    """Verify exact backup entries and their hashes before any simulated restore."""
    fd = os.open(manifest_name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=directory_fd)
    try:
        manifest_st = os.fstat(fd)
        if not stat.S_ISREG(manifest_st.st_mode) or manifest_st.st_nlink != 1:
            raise GatewayError("unsafe backup manifest")
        raw = bytearray()
        while True:
            block = os.read(fd, 65536)
            if not block:
                break
            raw.extend(block)
        manifest = json.loads(raw)
    finally:
        os.close(fd)
    entries = manifest.get("entries")
    if not isinstance(entries, dict) or not entries:
        raise GatewayError("invalid backup manifest entries")
    if set(os.listdir(directory_fd)) != set(entries) | {manifest_name}:
        raise GatewayError("backup directory entries differ from manifest")
    verified = {}
    for name, metadata in entries.items():
        if not _safe_leaf(name):
            raise GatewayError("invalid backup filename")
        file_fd = os.open(name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=directory_fd)
        try:
            st = os.fstat(file_fd)
            if not stat.S_ISREG(st.st_mode) or st.st_nlink != 1:
                raise GatewayError("unsafe backup object")
            digest, size = _digest_fd(file_fd)
            if (digest != metadata.get("sha256") or size != metadata.get("size")
                    or st.st_uid != metadata.get("uid") or st.st_gid != metadata.get("gid")
                    or stat.S_IMODE(st.st_mode) != metadata.get("mode")
                    or metadata.get("type") != "regular"):
                raise GatewayError(f"backup metadata or hash mismatch: {name}")
            verified[name] = {"sha256": digest, "size": size,
                              "uid": st.st_uid, "gid": st.st_gid,
                              "mode": stat.S_IMODE(st.st_mode)}
        finally:
            os.close(file_fd)
    return verified


def restore_backup_file(backup_dir_fd, target_dir_fd, name, *, manifest_name="manifest.json"):
    """Atomic simulation primitive. Caller must obtain external approval for real admin state."""
    if not _safe_leaf(name):
        raise GatewayError("invalid rollback destination")
    verified = validate_backup_manifest(backup_dir_fd, manifest_name)
    if name not in verified:
        raise GatewayError("rollback target is not in the verified backup manifest")
    try:
        existing = os.open(name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=target_dir_fd)
    except FileNotFoundError:
        existing = None
    if existing is not None:
        try:
            if not stat.S_ISREG(os.fstat(existing).st_mode):
                raise GatewayError("refusing to replace a non-regular rollback target")
        finally:
            os.close(existing)
    source_fd = os.open(name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=backup_dir_fd)
    temporary = ".restore-" + uuid.uuid4().hex
    target_fd = None
    try:
        target_fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
                            0o600, dir_fd=target_dir_fd)
        digest, size = _digest_fd(source_fd, target_fd)
        metadata = verified[name]
        if digest != metadata["sha256"] or size != metadata["size"]:
            raise GatewayError("backup changed during rollback copy")
        os.fchmod(target_fd, metadata["mode"])
        if os.geteuid() == 0:
            os.fchown(target_fd, metadata["uid"], metadata["gid"])
        os.fsync(target_fd)
        restored = os.fstat(target_fd)
        if (restored.st_uid != metadata["uid"] or restored.st_gid != metadata["gid"]
                or stat.S_IMODE(restored.st_mode) != metadata["mode"]):
            raise GatewayError("restored metadata mismatch")
        os.close(target_fd)
        target_fd = None
        os.rename(temporary, name, src_dir_fd=target_dir_fd, dst_dir_fd=target_dir_fd)
        os.fsync(target_dir_fd)
    except Exception:
        if target_fd is not None:
            os.close(target_fd)
        try:
            os.unlink(temporary, dir_fd=target_dir_fd)
        except FileNotFoundError:
            pass
        raise
    finally:
        os.close(source_fd)
    return verified[name]
