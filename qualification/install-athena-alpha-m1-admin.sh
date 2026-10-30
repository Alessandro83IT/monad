#!/bin/bash
set -euo pipefail
umask 077
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH

# Administrative installer draft. Never run it outside the disposable VM.
STAGE=/root/athena-alpha-m1-stage
RUNNER_SRC="$STAGE/athena-alpha-m1-runner.py"
SUPPORT_SRC="$STAGE/gateway_support.py"
SUDOERS_SRC="$STAGE/athena-alpha-m1-sudoers"
CANARY_SRC="$STAGE/athena-alpha-m1-systemd-canary.py"
RUNNER_DST=/usr/local/sbin/athena-alpha-m1-runner
SUPPORT_DST=/usr/local/sbin/gateway_support.py
SUDOERS_DST=/etc/sudoers.d/athena-alpha-m1
LOGDIR=/var/log/athena-alpha-m1
FIXTURE=/var/lib/athena-alpha-m1/ext4-fixture
BACKUP_ROOT=/var/lib/athena-alpha-m1/backups
CANARY_RECEIPT=/var/lib/athena-alpha-m1/systemd-canary.json
RUNNER_SHA256=c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def
SUPPORT_SHA256=92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514
SUDOERS_SHA256=263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18
CANARY_SHA256=9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2

# Rootless transaction harness. All paths are redirected beneath a caller-owned
# private root; this mode cannot be enabled by root and skips VM-only checks.
TEST_MODE=false
if [[ -n ${MONAD_ALPHA_M1_TEST_ROOT:-} ]]; then
    [[ $(id -u) -ne 0 ]] || { printf 'Test mode is forbidden as root.\n' >&2; exit 2; }
    TEST_ROOT=$MONAD_ALPHA_M1_TEST_ROOT
    [[ $TEST_ROOT == /* && $TEST_ROOT != *'/../'* && $TEST_ROOT != */.. && -d $TEST_ROOT && ! -L $TEST_ROOT ]] || { printf 'Invalid test root.\n' >&2; exit 2; }
    [[ $(stat -c '%u:%a' -- "$TEST_ROOT") == "$(id -u):700" ]] || { printf 'Test root must be caller-owned mode 0700.\n' >&2; exit 2; }
    TEST_ROOT=$(cd -- "$TEST_ROOT" && pwd -P)
    TEST_MODE=true
    STAGE="$TEST_ROOT/stage"
    RUNNER_SRC="$STAGE/athena-alpha-m1-runner.py"
    SUPPORT_SRC="$STAGE/gateway_support.py"
    SUDOERS_SRC="$STAGE/athena-alpha-m1-sudoers"
    CANARY_SRC="$STAGE/athena-alpha-m1-systemd-canary.py"
    RUNNER_DST="$TEST_ROOT/usr/local/sbin/athena-alpha-m1-runner"
    SUPPORT_DST="$TEST_ROOT/usr/local/sbin/gateway_support.py"
    SUDOERS_DST="$TEST_ROOT/etc/sudoers.d/athena-alpha-m1"
    LOGDIR="$TEST_ROOT/var/log/athena-alpha-m1"
    FIXTURE="$TEST_ROOT/var/lib/athena-alpha-m1/ext4-fixture"
    BACKUP_ROOT="$TEST_ROOT/var/lib/athena-alpha-m1/backups"
    CANARY_RECEIPT="$TEST_ROOT/var/lib/athena-alpha-m1/systemd-canary.json"
fi
FILE_UID=$(id -u)
FILE_GID=$(id -g)

STATE=
tmp_runner=
tmp_support=
tmp_sudoers=

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
need_root() { [[ $(id -u) -eq 0 ]] || die 'Run this administrative procedure as root.'; }

acl_clean() {
    local path=$1 entry acl
    command -v getfacl >/dev/null || return 1
    acl=$(getfacl -cp -- "$path") || return 1
    while IFS= read -r entry; do
        [[ -z $entry ]] && continue
        [[ $entry != default:* && $entry != mask:* ]] || return 1
        [[ ! $entry =~ ^user:[^:]+: && ! $entry =~ ^group:[^:]+: ]] || return 1
    done <<< "$acl"
}
check_acl() { [[ $TEST_MODE == true ]] && return 0; acl_clean "$1" || die "Cannot verify ACLs or extended ACL found: $1"; }

fsync_object() {
    /usr/bin/python3 - "$1" "$2" <<'PY'
import os, stat, sys
path, kind = sys.argv[1:]
flags = os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW
if kind == "directory":
    flags |= os.O_DIRECTORY
elif kind != "file":
    raise SystemExit("invalid fsync object kind")
fd = os.open(path, flags)
try:
    metadata = os.fstat(fd)
    if kind == "directory" and not stat.S_ISDIR(metadata.st_mode):
        raise SystemExit("expected directory for fsync")
    if kind == "file" and not stat.S_ISREG(metadata.st_mode):
        raise SystemExit("expected regular file for fsync")
    os.fsync(fd)
finally:
    os.close(fd)
PY
}

check_readonly_acl() {
    local path=$1 owner=$2 acl entry tag rest qualifier perms mask= rw effective
    command -v getfacl >/dev/null || die 'getfacl is required for explicit ACL validation.'
    acl=$(getfacl -cp -- "$path") || die "Cannot inspect ACLs: $path"
    while IFS= read -r entry; do
        [[ -n $entry ]] || continue
        [[ $entry != default:* ]] || die "Default ACL is not permitted on authenticated build paths: $path"
        [[ ${entry%%:*} == mask ]] && mask=${entry##*:}
    done <<< "$acl"
    while IFS= read -r entry; do
        [[ -n $entry ]] || continue
        tag=${entry%%:*}; rest=${entry#*:}; qualifier=${rest%%:*}; perms=${entry##*:}
        [[ $tag != mask ]] || continue
        [[ $perms != *w* ]] || {
            if [[ $tag == user && -z $qualifier && $owner -eq 0 ]]; then continue; fi
            if [[ $tag != user || -n $qualifier || $owner -ne 0 ]]; then
                if [[ $tag == other || $tag == user && -z $qualifier ]]; then die "Authenticated build ACL grants write access: $path ($entry)"; fi
                rw=0; effective=0
                [[ $perms == *r* ]] && ((effective |= 4))
                [[ $perms == *w* ]] && ((effective |= 2))
                [[ $perms == *x* ]] && ((effective |= 1))
                [[ $mask == *r* ]] && ((rw |= 4))
                [[ $mask == *w* ]] && ((rw |= 2))
                [[ $mask == *x* ]] && ((rw |= 1))
                (( (effective & rw & 2) == 0 )) || die "Authenticated build ACL grants effective write access: $path ($entry)"
            fi
        }
    done <<< "$acl"
}

check_components() {
    local path=$1 current=/ component uid mode mode_num sticky_tmp
    [[ $path == /* && $path != *//* && $path != */ ]] || die "Path must be absolute and normalized: $path"
    [[ $path != *'/../'* && $path != */.. && $path != */./* && $path != */. ]] || die "Dot component in path: $path"
    local -a parts
    IFS=/ read -r -a parts <<< "${path#/}"
    for component in "${parts[@]}"; do
        [[ -n $component ]] || continue
        current=${current%/}/$component
        [[ ! -L $current && -d $current ]] || die "Path component is missing, not a directory, or a symlink: $current"
        uid=$(stat -c %u -- "$current")
        mode=$(stat -c %a -- "$current")
        mode_num=$((8#$mode))
        sticky_tmp=false
        [[ $current == /var/tmp && $uid -eq 0 && $((mode_num & 01000)) -ne 0 ]] && sticky_tmp=true
        [[ $TEST_MODE == true && $current == /tmp && $((mode_num & 01000)) -ne 0 ]] && sticky_tmp=true
        if [[ $TEST_MODE == true && $current == /tmp && $sticky_tmp == true ]]; then
            :
        elif [[ $TEST_MODE == true && $current == "$TEST_ROOT"* ]]; then
            [[ $uid -eq $FILE_UID ]] || die "Test path component has unexpected owner: $current"
        else
            [[ $uid -eq 0 ]] || die "Path component must be root-owned: $current"
        fi
        if (( mode_num & 0022 )); then
            [[ $sticky_tmp == true ]] || die "Path component is writable by group/other: $current"
        fi
        if (( mode_num & 07000 )); then
            [[ $sticky_tmp == true && $((mode_num & 07000)) -eq 01000 ]] || die "Special mode bits are not permitted: $current"
        fi
        check_acl "$current"
    done
}

check_build_components() {
    local path=$1 current=/ component uid mode mode_num
    [[ $path == /opt/athena-qualification/build ]] || die "Unexpected protected build path: $path"
    local -a parts
    IFS=/ read -r -a parts <<< "${path#/}"
    for component in "${parts[@]}"; do
        [[ -n $component ]] || continue
        current=${current%/}/$component
        [[ ! -L $current && -d $current ]] || die "Protected build component is missing or a symlink: $current"
        uid=$(stat -c %u -- "$current")
        mode=$(stat -c %a -- "$current")
        mode_num=$((8#$mode))
        [[ $uid -eq 0 || $uid -eq 65534 ]] || die "Protected build owner is not root/nobody: $current"
        (( (mode_num & 0022) == 0 && (mode_num & 07000) == 0 )) || die "Protected build component has unsafe mode bits: $current"
        if [[ $uid -ne 0 ]]; then (( (mode_num & 0200) == 0 )) || die "Non-root protected build component is owner-writable: $current"; fi
        check_readonly_acl "$current" "$uid"
    done
}

safe_file() {
    local f=$1 mode
    check_components "${f%/*}"
    [[ -f $f && ! -L $f ]] || die "Expected regular, non-symlink file: $f"
    [[ $(stat -c %u -- "$f") -eq $FILE_UID ]] || die "File has unexpected owner: $f"
    mode=$(stat -c %a -- "$f")
    (( (8#$mode & 07022) == 0 )) || die "File has unsafe write or special mode bits: $f"
    check_acl "$f"
}

safe_artifact_file() {
    local f=$1 uid mode mode_num
    [[ $f == /opt/athena-qualification/build/* ]] || die "Unexpected authenticated artifact path: $f"
    check_build_components /opt/athena-qualification/build
    [[ -f $f && ! -L $f ]] || die "Expected regular, non-symlink authenticated artifact: $f"
    uid=$(stat -c %u -- "$f")
    mode=$(stat -c %a -- "$f")
    mode_num=$((8#$mode))
    [[ $uid -eq 0 || $uid -eq 65534 ]] || die "Authenticated artifact owner is not root/nobody: $f"
    (( (mode_num & 0022) == 0 && (mode_num & 07000) == 0 )) || die "Authenticated artifact has unsafe mode bits: $f"
    if [[ $uid -ne 0 ]]; then (( (mode_num & 0200) == 0 )) || die "Non-root authenticated artifact is owner-writable: $f"; fi
    check_readonly_acl "$f" "$uid"
}

safe_dir() {
    local dir=$1 expected=$2
    check_components "$dir"
    [[ $(stat -c '%u:%a' -- "$dir") == "$FILE_UID:$expected" ]] || die "Directory has unexpected owner/mode: $dir"
}

sha256() { sha256sum -- "$1" | cut -d' ' -f1; }

validate_backup_arg() {
    local supplied=$1 leaf
    [[ $supplied == "$BACKUP_ROOT/"* ]] || die "ROOT_BACKUP_DIR must be a direct child of $BACKUP_ROOT."
    leaf=${supplied#"$BACKUP_ROOT/"}
    [[ $leaf =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$ && $leaf != *.* ]] || die 'ROOT_BACKUP_DIR must use one simple, non-hidden directory name.'
    check_components "$BACKUP_ROOT"
    STATE=$supplied
}

ensure_fixture() {
    local fixture_entry
    check_components /var/lib/athena-alpha-m1
    [[ $(findmnt -n -T /var/lib/athena-alpha-m1 -o FSTYPE) == ext4 ]] || die 'The Alpha M1 state filesystem is not ext4; do not format or mount a device for this procedure.'
    if [[ -L $FIXTURE ]]; then die "Fixture path is a symlink: $FIXTURE"; fi
    if [[ ! -e $FIXTURE ]]; then
        mkdir -m 0700 -- "$FIXTURE" || die "Could not create ext4 fixture directory: $FIXTURE"
    fi
    safe_dir "$FIXTURE" 700
    [[ $(findmnt -n -T "$FIXTURE" -o FSTYPE) == ext4 ]] || die 'Fixture is not on ext4.'
    fixture_entry=$(find "$FIXTURE" -mindepth 1 -maxdepth 1 -print -quit) || die 'Could not inspect ext4 fixture contents.'
    [[ -z $fixture_entry ]] || die 'Ext4 fixture must be empty before staging.'
}

check_canary_receipt() {
    safe_file "$CANARY_RECEIPT"
    [[ $(stat -c %a -- "$CANARY_RECEIPT") == 600 ]] || die 'Canary receipt must be mode 0600.'
    /usr/bin/python3 - "$CANARY_RECEIPT" "$CANARY_SHA256" "$RUNNER_SHA256" <<'PY'
import json, os, shutil, stat, subprocess, sys, time
path, canary_hash, runner_hash = sys.argv[1:]
parts = path.split('/')
if not path.startswith('/') or any(p in ('', '.', '..') for p in parts[1:]):
    raise SystemExit('Canary receipt path is not normalized.')
parent_fd = os.open('/', os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
try:
    for component in parts[1:-1]:
        next_fd = os.open(component, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=parent_fd)
        os.close(parent_fd)
        parent_fd = next_fd
    fd = os.open(parts[-1], os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=parent_fd)
finally:
    os.close(parent_fd)
try:
    st = os.fstat(fd)
    if not stat.S_ISREG(st.st_mode) or st.st_uid != 0 or stat.S_IMODE(st.st_mode) != 0o600:
        raise SystemExit('Canary receipt metadata is unsafe.')
    with os.fdopen(os.dup(fd), 'r', encoding='utf-8') as stream:
        receipt = json.load(stream)
finally:
    os.close(fd)
now = time.time_ns()
validated = receipt.get('validated_ns')
boot_id = open('/proc/sys/kernel/random/boot_id', encoding='ascii').read().strip()
systemctl = shutil.which('systemctl', path='/usr/sbin:/usr/bin:/sbin:/bin')
if systemctl is None:
    raise SystemExit('systemctl is unavailable.')
systemd_version = subprocess.run([systemctl, '--version'], check=True,
                                 text=True, capture_output=True, timeout=5).stdout.splitlines()[0]
if (receipt.get('schema') != 1 or receipt.get('passed') is not True
        or receipt.get('canary_sha256') != canary_hash
        or receipt.get('runner_sha256') != runner_hash
        or not isinstance(validated, int) or validated > now + 300_000_000_000
        or now - validated > 86_400_000_000_000
        or receipt.get('boot_id') != boot_id
        or receipt.get('systemd_version') != systemd_version):
    raise SystemExit('Canary receipt is absent, failed, or stale for the staged runner.')
PY
}

check_install_paths() {
    local dir
    for dir in /usr/local/sbin /etc/sudoers.d /var/log/athena-alpha-m1 /var/lib/athena-alpha-m1; do
        check_components "$dir"
    done
    safe_dir "$LOGDIR" 700
    safe_dir /var/lib/athena-alpha-m1 700
    command -v systemd-run >/dev/null && command -v systemctl >/dev/null || die 'systemd-run/systemctl are unavailable.'
    [[ -x /usr/sbin/visudo ]] || die '/usr/sbin/visudo is unavailable.'
    [[ -x /usr/bin/python3 ]] || die '/usr/bin/python3 is unavailable.'
    command -v findmnt >/dev/null || die 'findmnt is unavailable.'
    id alessandro >/dev/null 2>&1 || die 'User alessandro is unavailable.'
    [[ -d /opt/athena-qualification/build && ! -L /opt/athena-qualification/build ]] || die 'Authenticated build path is unavailable.'
    check_build_components /opt/athena-qualification/build
}

check_prereqs() {
    need_root
    check_components "$STAGE"
    safe_file "$RUNNER_SRC"
    safe_file "$SUPPORT_SRC"
    safe_file "$SUDOERS_SRC"
    safe_file "$CANARY_SRC"
    [[ $(sha256 "$RUNNER_SRC") == "$RUNNER_SHA256" ]] || die 'Runner SHA-256 mismatch.'
    [[ $(sha256 "$SUPPORT_SRC") == "$SUPPORT_SHA256" ]] || die 'Gateway support SHA-256 mismatch.'
    [[ $(sha256 "$SUDOERS_SRC") == "$SUDOERS_SHA256" ]] || die 'Sudoers draft SHA-256 mismatch.'
    [[ $(sha256 "$CANARY_SRC") == "$CANARY_SHA256" ]] || die 'Systemd canary SHA-256 mismatch.'
    check_install_paths
    safe_dir "$BACKUP_ROOT" 700
    ensure_fixture
    check_canary_receipt
    local item expected actual artifact
    while read -r item expected; do
        artifact="/opt/athena-qualification/build/$item"
        safe_artifact_file "$artifact"
        actual=$(sha256 "$artifact")
        [[ $actual == "$expected" ]] || die "Authenticated artifact hash mismatch: $item"
    done <<'HASHES'
activation_execution_privileged_test d87a84cbb1e18ba073beaadb1bb7737b61fa448081af37e4d4c5a6c29337d157
activation_plan_retention_test f76251d6e0af0bd892d1dcb17f41f80b13343c7c274e289014dfcb090370e474
activation_plan_persistence_test f12620d89bb55e66f1e1960dd8852f2fdf27c9a148967d222d562fdddbd5aaa3
legacy_d3_test 61e23e8c9cd3a81c8766eb61e93e01cc7c9af2025a80f6056f28d3ac137ad6a9
initial_activation_test 934222dc181ae1a98a3d00bed5cba8e208cdad194d0c10fe552b1e5cf7e74243
libathena_core.so e212420cb34aeca76cebe2ecf396832f25c3c4f119b846b42ef00d2994cc995a
CTestTestfile.cmake 99751015487882864d67b948dbbb0eee4ac7204aca1b7ee20435f8e69b656bd5
HASHES
}

copy_back() {
    local key=$1 dst=$2 mode expected owner_group uid gid tmp
    state_file_ok "$STATE/$key" || { printf 'Unsafe backup file: %s\n' "$STATE/$key" >&2; return 1; }
    state_file_ok "$STATE/$key.mode" || { printf 'Unsafe saved mode file: %s\n' "$STATE/$key.mode" >&2; return 1; }
    state_file_ok "$STATE/$key.sha256" || { printf 'Unsafe saved digest file: %s\n' "$STATE/$key.sha256" >&2; return 1; }
    state_file_ok "$STATE/$key.owner-group" || { printf 'Unsafe saved ownership file: %s\n' "$STATE/$key.owner-group" >&2; return 1; }
    mode=$(cat -- "$STATE/$key.mode") || return 1
    [[ $mode =~ ^[0-7]{3,4}$ ]] || { printf 'Invalid saved mode for %s\n' "$key" >&2; return 1; }
    (( (8#$mode & 07022) == 0 )) || { printf 'Unsafe saved mode for %s\n' "$key" >&2; return 1; }
    expected=$(cat -- "$STATE/$key.sha256") || return 1
    [[ $expected =~ ^[0-9a-f]{64}$ ]] || { printf 'Invalid saved digest for %s\n' "$key" >&2; return 1; }
    owner_group=$(cat -- "$STATE/$key.owner-group") || return 1
    [[ $owner_group =~ ^[0-9]+:[0-9]+$ && ${owner_group%%:*} -eq $FILE_UID ]] || { printf 'Invalid saved owner/group for %s\n' "$key" >&2; return 1; }
    uid=${owner_group%%:*}; gid=${owner_group#*:}
    [[ $(sha256 "$STATE/$key") == "$expected" ]] || { printf 'Backup digest mismatch: %s\n' "$STATE/$key" >&2; return 1; }
    if [[ -e $dst || -L $dst ]]; then
        [[ -f $dst && ! -L $dst && $(stat -c %u -- "$dst") -eq $FILE_UID ]] || { printf 'Unsafe rollback destination: %s\n' "$dst" >&2; return 1; }
        acl_clean "$dst" || { printf 'Unsafe destination ACL: %s\n' "$dst" >&2; return 1; }
    fi
    tmp=$(mktemp "${dst%/*}/.athena-alpha-m1-restore.XXXXXX") || { printf 'Cannot create restore temporary for %s\n' "$dst" >&2; return 1; }
    local install_owner=()
    [[ $TEST_MODE == true ]] || install_owner=(-o "$uid" -g "$gid")
    if ! install "${install_owner[@]}" -m "$mode" -- "$STATE/$key" "$tmp" || ! mv -f -- "$tmp" "$dst"; then
        rm -f -- "$tmp"
        printf 'Could not restore %s from %s\n' "$dst" "$STATE/$key" >&2
        return 1
    fi
    fsync_object "$dst" file && fsync_object "${dst%/*}" directory || return 1
    [[ $(sha256 "$dst") == "$expected" && $(stat -c %a -- "$dst") == "$mode" && $(stat -c '%u:%g' -- "$dst") == "$owner_group" ]] || { printf 'Restored target verification failed: %s\n' "$dst" >&2; return 1; }
    acl_clean "$dst" || { printf 'Restored file has an extended ACL: %s\n' "$dst" >&2; return 1; }
}

state_file_ok() {
    local path=$1
    [[ -f $path && ! -L $path && $(stat -c '%u:%a' -- "$path") == "$FILE_UID:600" ]] && acl_clean "$path"
}

backup_manifest() {
    /usr/bin/python3 - "$STATE" "$1" "$FILE_UID" "$FILE_GID" <<'PY'
import hashlib, json, os, re, stat, sys

directory, action = sys.argv[1:3]
owner_uid, owner_gid = map(int, sys.argv[3:5])
keys = ("runner", "support", "sudoers")
directory_fd = os.open(directory, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
manifest_name = "backup-manifest.json"

def read_file(name):
    fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=directory_fd)
    try:
        st = os.fstat(fd)
        if (not stat.S_ISREG(st.st_mode) or st.st_uid != owner_uid or st.st_gid != owner_gid
                or st.st_nlink != 1 or stat.S_IMODE(st.st_mode) != 0o600):
            raise ValueError("unsafe backup file metadata: " + name)
        if any(x.startswith("system.posix_acl_") for x in os.listxattr(fd)):
            raise ValueError("ACL is not permitted on backup file: " + name)
        if st.st_size > 64 * 1024 * 1024:
            raise ValueError("backup entry exceeds size limit: " + name)
        digest = hashlib.sha256()
        blocks = []
        while True:
            block = os.read(fd, 1024 * 1024)
            if not block:
                break
            digest.update(block)
            blocks.append(block)
        if action == "write":
            os.fsync(fd)
        return {"name": name, "sha256": digest.hexdigest(), "size": st.st_size,
                "uid": st.st_uid, "gid": st.st_gid, "mode": stat.S_IMODE(st.st_mode),
                "type": "regular"}, b"".join(blocks)
    finally:
        os.close(fd)

try:
    entries = {}
    for key in keys:
        present = key + ".existed"
        absent = key + ".absent"
        names = set(os.listdir(directory_fd))
        if (present in names) == (absent in names):
            raise ValueError("backup must have exactly one existence marker: " + key)
        selected = [present] if present in names else [absent]
        if present in names:
            selected += [key, key + ".mode", key + ".sha256", key + ".owner-group"]
        for name in selected:
            metadata, raw = read_file(name)
            entries[name] = metadata
        if present in names:
            mode = read_file(key + ".mode")[1].decode("ascii").strip()
            expected_hash = read_file(key + ".sha256")[1].decode("ascii").strip()
            owner_group = read_file(key + ".owner-group")[1].decode("ascii").strip()
            if not re.fullmatch(r"[0-7]{3,4}", mode) or (int(mode, 8) & 0o7022):
                raise ValueError("unsafe recorded original mode: " + key)
            if not re.fullmatch(r"[0-9a-f]{64}", expected_hash):
                raise ValueError("invalid recorded original digest: " + key)
            if not re.fullmatch(r"[0-9]+:[0-9]+", owner_group):
                raise ValueError("invalid recorded original owner/group: " + key)
            if entries[key]["sha256"] != expected_hash:
                raise ValueError("backup payload differs from its digest record: " + key)

    if action == "write":
        document = {"schema": 1, "entries": entries}
        raw = (json.dumps(document, sort_keys=True, separators=(",", ":")) + "\n").encode()
        fd = os.open(manifest_name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                     0o600, dir_fd=directory_fd)
        try:
            view = memoryview(raw)
            while view:
                count = os.write(fd, view)
                if count <= 0:
                    raise OSError("short backup-manifest write")
                view = view[count:]
            os.fsync(fd)
        finally:
            os.close(fd)
        os.fsync(directory_fd)
    elif action == "verify":
        manifest_meta, raw = read_file(manifest_name)
        document = json.loads(raw)
        if document != {"schema": 1, "entries": entries}:
            raise ValueError("backup manifest, file inventory, or metadata mismatch")
        allowed = set(entries) | {manifest_name, "install-started", "transaction.json", "prepared", "install-partial", "install-complete", "rollback-started", "rollback-partial", "rollback-complete"}
        transaction_raw = read_file("transaction.json")[1]
        transaction = json.loads(transaction_raw)
        transaction_id = transaction.get("transaction_id")
        if not isinstance(transaction_id, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", transaction_id):
            raise ValueError("invalid transaction identity")
        marker_names = {"prepared", "install-started", "install-partial", "install-complete", "rollback-started", "rollback-partial", "rollback-complete"}
        present = set(os.listdir(directory_fd)) & marker_names
        for name in present:
            marker = read_file(name)[1].decode("ascii")
            if name == "install-partial":
                if not re.fullmatch(r"transaction=" + re.escape(transaction_id) + r" exit=[0-9]+\n", marker):
                    raise ValueError("invalid partial install marker")
            elif marker != "transaction=" + transaction_id + "\n":
                raise ValueError("transaction marker identity mismatch: " + name)
        for name in os.listdir(directory_fd):
            if name in {"rollback-runner.status", "rollback-support.status", "rollback-sudoers.status"}:
                read_file(name)
                allowed.add(name)
        actual_names = set(os.listdir(directory_fd))
        required_names = set(entries) | {manifest_name, "transaction.json", "prepared", "install-started"}
        if not required_names.issubset(actual_names) or not actual_names.issubset(allowed):
            raise ValueError("unexpected or missing file in administrative backup directory")
    else:
        raise ValueError("invalid manifest action")
except Exception as exc:
    print("Backup manifest failure: " + str(exc), file=sys.stderr)
    sys.exit(1)
finally:
    os.close(directory_fd)
PY
}

write_rollback_status() {
    local key=$1 value=$2 path tmp
    path=$STATE/rollback-$key.status
    if [[ -e $path || -L $path ]]; then
        state_file_ok "$path" || return 1
    fi
    tmp=$(mktemp "$STATE/.rollback-status.XXXXXX") || return 1
    if ! printf '%s\n' "$value" > "$tmp" || ! chmod 0600 -- "$tmp" || ! acl_clean "$tmp" || ! mv -f -- "$tmp" "$path"; then
        rm -f -- "$tmp"
        return 1
    fi
    fsync_object "$path" file && fsync_object "$STATE" directory || return 1
}

rollback_one() {
    local key=$1 dst=$2 existed absent
    existed="$STATE/$key.existed"
    absent="$STATE/$key.absent"
    if [[ -e $existed || -L $existed ]] && [[ -e $absent || -L $absent ]]; then
        printf 'Conflicting rollback markers for %s; no action taken.\n' "$key" >&2
        return 1
    fi
    if [[ -e $existed || -L $existed ]]; then
        state_file_ok "$existed" || { printf 'Unsafe rollback marker: %s\n' "$existed" >&2; return 1; }
        copy_back "$key" "$dst"
    elif [[ -e $absent || -L $absent ]]; then
        state_file_ok "$absent" || { printf 'Unsafe rollback marker: %s\n' "$absent" >&2; return 1; }
        if [[ -e $dst || -L $dst ]]; then
            [[ -f $dst && ! -L $dst && $(stat -c %u -- "$dst") -eq $FILE_UID ]] || { printf 'Refusing to remove unsafe destination: %s\n' "$dst" >&2; return 1; }
            acl_clean "$dst" || { printf 'Unsafe destination ACL: %s\n' "$dst" >&2; return 1; }
            rm -f -- "$dst" || return 1
            fsync_object "${dst%/*}" directory || return 1
        fi
        [[ ! -e $dst && ! -L $dst ]]
    else
        printf 'Incomplete rollback state for %s; no action taken.\n' "$key" >&2
        return 1
    fi
}

rollback_all() {
    local failures=0 key dst
    backup_manifest verify || {
        printf 'Backup manifest validation failed; no administrative targets restored. Preserve %s.\n' "$STATE" >&2
        return 1
    }
    for pair in "runner:$RUNNER_DST" "support:$SUPPORT_DST" "sudoers:$SUDOERS_DST"; do
        key=${pair%%:*}; dst=${pair#*:}
        if [[ -e $dst || -L $dst ]]; then
            [[ -f $dst && ! -L $dst && $(stat -c %u -- "$dst") -eq $FILE_UID ]] || {
                printf 'Unsafe rollback destination: %s; no targets restored.\n' "$dst" >&2
                return 1
            }
            acl_clean "$dst" || {
                printf 'Unsafe rollback destination ACL: %s; no targets restored.\n' "$dst" >&2
                return 1
            }
        fi
    done
    for key in runner support sudoers; do
        dst=$RUNNER_DST
        [[ $key == support ]] && dst=$SUPPORT_DST
        [[ $key == sudoers ]] && dst=$SUDOERS_DST
        if rollback_one "$key" "$dst"; then
            if ! write_rollback_status "$key" complete; then
                printf 'Could not record successful rollback status for %s.\n' "$key" >&2
                failures=1
                break
            fi
            if [[ $key == runner ]] && ! fault_point after-rollback-runner-restore; then
                write_rollback_status "$key" 'restored before injected interruption' || true
                printf 'Injected failure after runner restore; stopping further rollback changes.\n' >&2
                failures=1
                break
            fi
        else
            write_rollback_status "$key" 'partial or failed' || printf 'Could not record rollback failure status for %s.\n' "$key" >&2
            printf 'PARTIAL ROLLBACK: preserve %s; restore %s manually from %s after reviewing metadata and ACLs.\n' "$STATE" "$dst" "$STATE/$key" >&2
            failures=1
            break
        fi
    done
    if (( failures == 0 )) && [[ -f $STATE/logdir-created && ! -L $STATE/logdir-created ]]; then
        if ! rmdir -- "$LOGDIR" 2>/dev/null; then
            printf 'Log directory remains (nonempty or changed); preserve and inspect: %s\n' "$LOGDIR" >&2
        fi
    fi
    if [[ $TEST_MODE != true ]] && ! /usr/sbin/visudo -c; then
        printf 'PARTIAL ROLLBACK: visudo validation failed; preserve %s and inspect both sudoers files.\n' "$STATE" >&2
        failures=1
    fi
    return "$failures"
}

write_transaction_record() {
    /usr/bin/python3 - "$STATE" "$RUNNER_DST" "$SUPPORT_DST" "$SUDOERS_DST" "$RUNNER_SRC" "$SUPPORT_SRC" "$SUDOERS_SRC" <<'PY'
import hashlib,json,os,stat,sys
state,*paths=sys.argv[1:]; dst=paths[:3]; src=paths[3:]
def digest(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for b in iter(lambda:f.read(1048576),b''): h.update(b)
 return h.hexdigest()
originals=[]
for key,p in zip(('runner','support','sudoers'),dst):
 if os.path.lexists(p):
  s=os.lstat(p)
  if not stat.S_ISREG(s.st_mode): raise SystemExit('unsafe original target')
  h=digest(p)
  if digest(os.path.join(state,key))!=h: raise SystemExit('original target changed after backup capture')
  saved_mode=open(os.path.join(state,key+'.mode'),encoding='ascii').read().strip()
  saved_owner=open(os.path.join(state,key+'.owner-group'),encoding='ascii').read().strip()
  if saved_mode!=format(stat.S_IMODE(s.st_mode),'o') or saved_owner!=f'{s.st_uid}:{s.st_gid}': raise SystemExit('original target metadata changed after backup capture')
  originals.append({'present':True,'sha256':h,'uid':s.st_uid,'gid':s.st_gid,'mode':stat.S_IMODE(s.st_mode)})
 else:
  if not os.path.isfile(os.path.join(state,key+'.absent')): raise SystemExit('absence state differs from backup capture')
  originals.append({'present':False})
m=digest(os.path.join(state,'backup-manifest.json'))
doc={'schema':1,'transaction_id':os.path.basename(state),'backup_manifest_sha256':m,'destinations':dst,'originals':originals,'candidates':[digest(p) for p in src]}
raw=(json.dumps(doc,sort_keys=True,separators=(',',':'))+'\n').encode()
fd=os.open(os.path.join(state,'transaction.json'),os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
try:
 view=memoryview(raw)
 while view:
  n=os.write(fd,view)
  if n<=0: raise OSError('short transaction record write')
  view=view[n:]
 os.fsync(fd)
finally: os.close(fd)
d=os.open(state,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW); os.fsync(d); os.close(d)
PY
}

verify_rollback_authorization() {
    local approval=$1 supplied_id=$2
    /usr/bin/python3 - "$STATE" "$approval" "$supplied_id" "$FILE_UID" "$RUNNER_DST" "$SUPPORT_DST" "$SUDOERS_DST" "$APPROVAL_SHA256" <<'PY'
import hashlib,json,os,stat,sys
state,approval,tid,uid,*tail=sys.argv[1:]; uid=int(uid); expected_destinations=tail[:3]; expected_approval_hash=tail[3]
def open_nofollow(path):
 if not path.startswith('/') or any(x in ('','.','..') for x in path.split('/')[1:]): raise ValueError('unnormalized path')
 parts=path.split('/')[1:]; d=os.open('/',os.O_RDONLY|os.O_DIRECTORY)
 try:
  for p in parts[:-1]:
   n=os.open(p,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW,dir_fd=d); os.close(d); d=n
  f=os.open(parts[-1],os.O_RDONLY|os.O_NOFOLLOW,dir_fd=d)
 finally: os.close(d)
 return f
def read(path):
 f=open_nofollow(path)
 try:
  s=os.fstat(f)
  if not stat.S_ISREG(s.st_mode) or s.st_uid!=uid or stat.S_IMODE(s.st_mode)!=0o600 or s.st_nlink!=1: raise ValueError('unsafe approval/record metadata')
  if any(x.startswith('system.posix_acl_') for x in os.listxattr(f)): raise ValueError('ACL not permitted on approval/record')
  b=b''
  while True:
   x=os.read(f,65536)
   if not x: break
   b+=x
   if len(b)>1048576: raise ValueError('oversized record')
  return b
 finally: os.close(f)
try:
 raw=read(state+'/transaction.json'); tx=json.loads(raw)
 if raw!=(json.dumps(tx,sort_keys=True,separators=(',',':'))+'\n').encode(): raise ValueError('noncanonical transaction record')
 if set(tx)!={'schema','transaction_id','backup_manifest_sha256','destinations','originals','candidates'} or tx.get('schema')!=1: raise ValueError('unsupported transaction schema or fields')
 if tx.get('destinations')!=expected_destinations or len(tx.get('originals',[]))!=3 or len(tx.get('candidates',[]))!=3: raise ValueError('transaction target inventory mismatch')
 for h in tx['candidates']:
  if not isinstance(h,str) or len(h)!=64 or any(c not in '0123456789abcdef' for c in h): raise ValueError('invalid candidate digest')
 for o in tx['originals']:
  if o=={'present':False}: continue
  if set(o)!={'present','sha256','uid','gid','mode'} or o.get('present') is not True or not isinstance(o.get('sha256'),str) or len(o['sha256'])!=64: raise ValueError('invalid original metadata')
 mr=read(state+'/backup-manifest.json'); mh=hashlib.sha256(mr).hexdigest(); th=hashlib.sha256(raw).hexdigest()
 if tx.get('transaction_id')!=tid or tid!=os.path.basename(state) or tx.get('backup_manifest_sha256')!=mh: raise ValueError('transaction binding mismatch')
 ar=read(approval)
 if hashlib.sha256(ar).hexdigest()!=expected_approval_hash: raise ValueError('approval file digest mismatch')
 token=json.loads(ar)
 if ar!=(json.dumps(token,sort_keys=True,separators=(',',':'))+'\n').encode(): raise ValueError('noncanonical approval record')
 if token!={'schema':1,'action':'rollback','transaction_id':tid,'backup_manifest_sha256':mh,'transaction_sha256':th}: raise ValueError('approval token binding mismatch')
 for p,o,c in zip(tx['destinations'],tx['originals'],tx['candidates']):
  if os.path.lexists(p):
   fd=open_nofollow(p); s=os.fstat(fd)
   if not stat.S_ISREG(s.st_mode): os.close(fd); raise ValueError('unsafe target')
   hh=hashlib.sha256()
   while True:
    b=os.read(fd,1048576)
    if not b: break
    hh.update(b)
   os.close(fd); h=hh.hexdigest()
   if h not in ({c,o['sha256']} if o.get('present') else {c}): raise ValueError('target is neither recorded generation')
  elif o.get('present'): raise ValueError('recorded target missing')
except Exception as e: print('Rollback authorization/identity validation failed: '+str(e),file=sys.stderr); sys.exit(1)
PY
}

on_install_exit() {
    local rc=$?
    trap - EXIT
    set +e
    if [[ $rc -ne 0 && -n $STATE && -f $STATE/install-started && ! -f $STATE/install-complete ]]; then
        printf 'Install failed after publication began. Targets may be mixed; preserving backup and transaction evidence.\n' >&2
        mark_once install-partial "transaction=${STATE##*/} exit=$rc" || true
    fi
    if [[ ! -f $STATE/install-started ]]; then
        [[ -z $tmp_runner ]] || rm -f -- "$tmp_runner"
        [[ -z $tmp_support ]] || rm -f -- "$tmp_support"
        [[ -z $tmp_sudoers ]] || rm -f -- "$tmp_sudoers"
    fi
    exit "$rc"
}

mark_once() {
    local name=$1 value=$2 path=$STATE/$1 fd
    [[ ! -e $path && ! -L $path ]] || return 1
    (umask 077; set -o noclobber; printf '%s\n' "$value" > "$path") || return 1
    fsync_object "$path" file && fsync_object "$STATE" directory
}

fault_point() {
    local point=$1 wanted=${MONAD_ALPHA_M1_FAULT_POINT:-}
    [[ $TEST_MODE == true ]] || return 0
    [[ $wanted == "$point" || $wanted == "kill-$point" ]] || return 0
    [[ $wanted == kill-* ]] && kill -KILL $$
    return 91
}

test_pause() {
    local point=$1 marker
    [[ $TEST_MODE == true && ${MONAD_ALPHA_M1_TEST_PAUSE_AT:-} == "$point" ]] || return 0
    marker="$TEST_ROOT/release-$point"
    local i
    for ((i=0; i<300; i++)); do [[ -e $marker ]] && return 0; sleep 0.1; done
    printf 'Timed out at test pause point %s\n' "$point" >&2
    return 1
}

acquire_lock() {
    local lock_path=/var/lib/athena-alpha-m1/admin-install.lock
    if [[ $TEST_MODE == true ]]; then lock_path="$TEST_ROOT/var/lib/athena-alpha-m1/admin-install.lock"; fi
    check_components "${lock_path%/*}"
    if [[ -e $lock_path || -L $lock_path ]]; then
        [[ -f $lock_path && ! -L $lock_path && $(stat -c '%u:%a:%h' -- "$lock_path") == "$FILE_UID:600:1" ]] || die 'Administrative lock path is unsafe.'
    else
        (umask 077; set -o noclobber; : > "$lock_path") || die 'Cannot create administrative lock file.'
    fi
    exec 9<>"$lock_path"
    [[ -f /proc/$$/fd/9 && $(stat -Lc '%u:%a:%h' -- "/proc/$$/fd/9") == "$FILE_UID:600:1" ]] || die 'Opened administrative lock object is unsafe.'
    flock -n 9 || die 'Another Alpha M1 administrative transaction holds the lock.'
    if [[ $TEST_MODE == true && ${MONAD_ALPHA_M1_TEST_HOLD_LOCK:-} == 1 ]]; then sleep 3; fi
}

refuse_incomplete_transactions() {
    local dir
    shopt -s nullglob
    for dir in "$BACKUP_ROOT"/*; do
        [[ ${dir##*/} == admin-install.lock ]] && continue
        [[ -d $dir && ! -L $dir ]] || die "Unexpected entry in transaction backup root: $dir"
        if [[ ( ! -e $dir/install-complete && ! -e $dir/rollback-complete ) || ( -e $dir/rollback-started && ! -e $dir/rollback-complete ) ]]; then
            die "Incomplete transaction blocks new install; preserve and review: $dir"
        fi
    done
    shopt -u nullglob
}

verify_installed() {
    safe_file "$RUNNER_DST"
    [[ $(stat -c '%u:%g:%a' -- "$RUNNER_DST") == 0:0:755 ]] || die 'Installed runner owner/mode verification failed.'
    [[ $(sha256 "$RUNNER_DST") == "$RUNNER_SHA256" ]] || die 'Installed runner SHA-256 verification failed.'
    safe_file "$SUPPORT_DST"
    [[ $(stat -c '%u:%g:%a' -- "$SUPPORT_DST") == 0:0:644 ]] || die 'Installed support owner/mode verification failed.'
    [[ $(sha256 "$SUPPORT_DST") == "$SUPPORT_SHA256" ]] || die 'Installed gateway support SHA-256 verification failed.'
    safe_file "$SUDOERS_DST"
    [[ $(stat -c '%u:%g:%a' -- "$SUDOERS_DST") == 0:0:440 ]] || die 'Installed sudoers owner/mode verification failed.'
    [[ $(sha256 "$SUDOERS_DST") == "$SUDOERS_SHA256" ]] || die 'Installed sudoers SHA-256 verification failed.'
    safe_dir "$LOGDIR" 700
    /usr/sbin/visudo -cf "$SUDOERS_DST"
    /usr/sbin/visudo -c
}

usage() { die "Usage: $0 install ROOT_BACKUP_DIR | rollback ROOT_BACKUP_DIR --transaction-id ID --approval-file FILE --approval-sha256 SHA256 | prepare-fixture"; }

if [[ $# -eq 1 && $1 == prepare-fixture ]]; then
    need_root
    ensure_fixture
    printf 'Prepared empty root-owned ext4 fixture: %s\n' "$FIXTURE"
    exit 0
fi
[[ ( $# -eq 2 && $1 == install ) || ( $# -eq 8 && $1 == rollback && $3 == --transaction-id && $5 == --approval-file && $7 == --approval-sha256 ) ]] || usage
ACTION=$1
validate_backup_arg "$2"
if [[ $ACTION == rollback ]]; then APPROVED_ID=$4; APPROVAL_FILE=$6; APPROVAL_SHA256=$8; [[ $APPROVAL_SHA256 =~ ^[0-9a-f]{64}$ ]] || usage; fi
if [[ $TEST_MODE != true ]]; then need_root; fi
if [[ $TEST_MODE == true ]]; then
    [[ $STATE == "$TEST_ROOT"/var/lib/athena-alpha-m1/backups/* ]] || die 'Test transaction escaped test root.'
    [[ -d $STAGE && -d ${RUNNER_DST%/*} && -d ${SUPPORT_DST%/*} && -d ${SUDOERS_DST%/*} && -d $BACKUP_ROOT ]] || die 'Test root layout is incomplete.'
else
    acquire_lock
fi
if [[ $TEST_MODE == true ]]; then acquire_lock; fi

if [[ $ACTION == install ]]; then
    if [[ $TEST_MODE == true ]]; then
        for f in "$RUNNER_SRC" "$SUPPORT_SRC" "$SUDOERS_SRC"; do [[ -f $f && ! -L $f ]] || die "Missing test source: $f"; done
    else
        check_prereqs
    fi
    [[ ! -e $STATE && ! -L $STATE ]] || die "Backup path already exists: $STATE"
    refuse_incomplete_transactions
    mkdir -m 0700 -- "$STATE" || die "Cannot create protected backup directory: $STATE"
    safe_dir "$STATE" 700
    trap on_install_exit EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    for pair in "runner:$RUNNER_DST" "support:$SUPPORT_DST" "sudoers:$SUDOERS_DST"; do
        key=${pair%%:*}; dst=${pair#*:}
        if [[ -e $dst || -L $dst ]]; then
            safe_file "$dst"
            local_install=()
            [[ $TEST_MODE == true ]] || local_install=(-o root -g root)
            install "${local_install[@]}" -m 0600 -- "$dst" "$STATE/$key"
            stat -c %a -- "$dst" > "$STATE/$key.mode"
            sha256 "$dst" > "$STATE/$key.sha256"
            stat -c '%u:%g' -- "$dst" > "$STATE/$key.owner-group"
            touch "$STATE/$key.existed"
        else
            touch "$STATE/$key.absent"
        fi
    done
    chmod 0600 -- "$STATE"/*
    for f in "$STATE"/*; do check_acl "$f"; done
    backup_manifest write || die 'Could not write and sync aggregate backup manifest.'
    check_acl "$STATE/backup-manifest.json"
    write_transaction_record || die 'Could not write transaction identity record.'
    mark_once prepared "transaction=$(/usr/bin/basename -- "$STATE")" || die 'Could not write prepared marker.'
    tmp_runner=$(mktemp "${RUNNER_DST%/*}/.athena-alpha-m1-runner.XXXXXX")
    tmp_support=$(mktemp "${SUPPORT_DST%/*}/.athena-alpha-m1-support.XXXXXX")
    tmp_sudoers=$(mktemp "${SUDOERS_DST%/*}/.athena-alpha-m1.XXXXXX")
    local_install=()
    [[ $TEST_MODE == true ]] || local_install=(-o root -g root)
    install "${local_install[@]}" -m 0755 -- "$RUNNER_SRC" "$tmp_runner"
    install "${local_install[@]}" -m 0644 -- "$SUPPORT_SRC" "$tmp_support"
    install "${local_install[@]}" -m 0440 -- "$SUDOERS_SRC" "$tmp_sudoers"
    fsync_object "$tmp_runner" file
    fsync_object "$tmp_support" file
    fsync_object "$tmp_sudoers" file
    [[ $TEST_MODE == true ]] || /usr/sbin/visudo -cf "$tmp_sudoers"
    fault_point before-install-started || die 'Injected test fault before marker.'
    mark_once install-started "transaction=$(/usr/bin/basename -- "$STATE")" || die 'Could not persist install-started marker.'
    fault_point after-install-started || die 'Injected test fault after install-started.'
    test_pause after-install-started || die 'Test pause failed after install-started.'
    mv -f -- "$tmp_runner" "$RUNNER_DST"
    fsync_object "${RUNNER_DST%/*}" directory
    tmp_runner=
    fault_point after-runner-rename || die 'Injected fault after runner rename.'
    mv -f -- "$tmp_support" "$SUPPORT_DST"
    fsync_object "${SUPPORT_DST%/*}" directory
    tmp_support=
    fault_point after-support-rename || die 'Injected fault after support rename.'
    mv -f -- "$tmp_sudoers" "$SUDOERS_DST"
    fsync_object "${SUDOERS_DST%/*}" directory
    tmp_sudoers=
    fault_point after-sudoers-rename || die 'Injected fault after sudoers rename.'
    if [[ $TEST_MODE == true ]]; then
        [[ $(sha256 "$RUNNER_DST") == "$(sha256 "$RUNNER_SRC")" && $(sha256 "$SUPPORT_DST") == "$(sha256 "$SUPPORT_SRC")" && $(sha256 "$SUDOERS_DST") == "$(sha256 "$SUDOERS_SRC")" ]] || die 'Installed test generation hash mismatch.'
    else verify_installed; fi
    mark_once install-complete "transaction=$(/usr/bin/basename -- "$STATE")" || die 'Could not persist completion marker.'
    trap - EXIT
    printf 'Installed transaction %s. Rollback requires a separately approved token bound to transaction.json and backup-manifest.json.\n' "${STATE##*/}"
else
    safe_dir "$STATE" 700
    [[ $TEST_MODE == true ]] || check_install_paths
    [[ $APPROVED_ID == "${STATE##*/}" ]] || die 'Explicit transaction ID does not match backup directory.'
    [[ -f $STATE/install-started && ! -f $STATE/rollback-started ]] || die 'Transaction is not rollback-eligible or a rollback was already attempted.'
    verify_rollback_authorization "$APPROVAL_FILE" "$APPROVED_ID" || die 'Rollback approval/identity check rejected; backup and receipt preserved.'
    backup_manifest verify || die 'Backup manifest validation failed; no administrative targets restored.'
    mark_once rollback-started "transaction=$APPROVED_ID" || die 'Could not persist rollback-started marker.'
    if ! rollback_all; then
        mark_once rollback-partial "transaction=$APPROVED_ID" || true
        die "Rollback was partial. Preserve $STATE and follow the manual recovery instructions in the qualification documentation."
    fi
    mark_once rollback-complete "transaction=$APPROVED_ID" || die 'Rollback completed but completion marker failed; preserve transaction.'
    printf '%s\n' 'Previous runner and sudoers state restored; backup retained for audit.'
fi
