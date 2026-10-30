#!/usr/bin/env bash
# Read-only evidence collector for the dedicated Alpha M1 Debian VM.
# The only filesystem writes are files in one new, private evidence directory.
set -u
set -o pipefail
export LC_ALL=C
PATH='/usr/sbin:/usr/bin:/sbin:/bin'
export PATH
umask 077

on_unhandled_error() {
    local rc=$?
    trap - ERR
    printf 'FATAL: evidence collection stopped after an unexpected command or output error (exit=%s, line=%s). Partial evidence, if any, was not cleaned up.\n' "$rc" "${BASH_LINENO[0]:-unknown}" >&2 || true
    exit 74
}
set -E
trap 'on_unhandled_error' ERR

readonly BASELINE_INSTALLER='2376f43c5cbc91e0ee814b8e9a4b0024015ae4db54c01ed0a69ea45204128dc7'
readonly BASELINE_MANIFEST='00fd4036bf0f3873f5c2a0e932c912b4709460fa52c35a1c2564016a92811262'
readonly BASELINE_RUNNER='c68bca67a5a85ef64987c20f28f961b08d55dfec8f5ce905598ed5c7d7e19def'
readonly BASELINE_SUPPORT='92c928ca3c6e344d240d29bd8bc212d2bf214aba899ca53818c80745cb5cd514'
readonly BASELINE_CANARY='9d9d4d845c896241b285e102a8d0117d401c495124ea442edb270d3a3e2a94d2'
readonly BASELINE_SUDOERS='263c1197b2aac1f9fd312eb19b99b673506e647ef715b85c0ee6c81c54f36c18'
readonly REVIEWED_CTEST_HELPER_SHA256='1d95eb5c781f4f21581a01a2b778e097535221af3b1a9b7610ad81cf576c87d1'
readonly PROTECTED_BUILD='/opt/athena-qualification/build'
readonly ISOLATED_BUILD='/tmp/monad-s32-verification-final4-20261021'
readonly STATE_ROOT='/var/lib/athena-alpha-m1'
readonly FIXTURE="$STATE_ROOT/ext4-fixture"
readonly RECEIPT="$STATE_ROOT/systemd-canary.json"
readonly BACKUPS="$STATE_ROOT/backups"
readonly LOG_ROOT='/var/log/athena-alpha-m1'
readonly D6_PARENT='/var/tmp/athena-d67-ext4'
readonly D6_RESIDUAL="$D6_PARENT/athena-d6-8yRDjc"
readonly EXPECTED_PRIVILEGED='activation_execution_privileged_sources_test
activation_execution_privileged_legacy_test
activation_execution_privileged_owner_test
activation_execution_privileged_bind_test
activation_execution_privileged_nested_test
activation_execution_privileged_ext4_test
activation_plan_retention_privileged_owner_test
activation_plan_retention_privileged_bind_test
activation_plan_retention_privileged_nested_test
activation_plan_retention_privileged_ext4_test
activation_plan_persistence_privileged_test
legacy_d3_scanner_test
activation_plan_persistence_privileged_owner_test
activation_plan_persistence_privileged_bind_test
activation_plan_persistence_privileged_nested_test
activation_plan_persistence_privileged_tmpfs_test
activation_plan_persistence_privileged_ext4_test
initial_activation_migration_test'
readonly SESSION35_ALPHA_UNITS='athena-alpha-m1-28cc5aa5ec9a7fd1e7c6b715.service
athena-alpha-m1-2ba07ed2f4ad03055baff910.service
athena-alpha-m1-40ca148184e8131b74789e21.service
athena-alpha-m1-43ff39862d99ed8e68d729ae.service
athena-alpha-m1-45f1c7159a426941d20fed8a.service
athena-alpha-m1-5c40304f1dae004fc15e3830.service
athena-alpha-m1-6633d84535fa91142caf6bd5.service
athena-alpha-m1-666aa836f5d15842d47bdef3.service
athena-alpha-m1-716ab14bcbb4b154f5953042.service
athena-alpha-m1-7fe9a30496fd95c631ea7c4b.service
athena-alpha-m1-88fb1f423d5274d96d5ce40b.service
athena-alpha-m1-958addae253edbe229d4917b.service
athena-alpha-m1-c720b53df3240534303448a8.service
athena-alpha-m1-fc331e22979bcd4126863547.service
athena-alpha-m1-ff5c72902fe3a6513080384a.service'

usage() {
    cat <<'EOF'
Usage: alpha-m1-vm-evidence-collector.sh --output-parent ABSOLUTE_EXISTING_DIR --candidate-dir ABSOLUTE_QUALIFICATION_DIR

Run manually in a shell on the intended Debian qualification VM. The output
parent must be an existing directory owned by the current user, not a symlink,
and not group/world writable. The candidate directory must be an existing
canonical non-symlink directory supplied explicitly; it is never inferred from
the script path. The collector creates one new mode-0700 child directory and
writes evidence only there. This script never invokes sudo, executes
CTest cases, changes system state, or authenticates a VM snapshot.
EOF
}

OUTPUT_PARENT=''
CANDIDATE_DIR=''
while (($#)); do
    case "$1" in
        --output-parent)
            (($# >= 2)) || { usage >&2; exit 64; }
            OUTPUT_PARENT=$2
            shift 2
            ;;
        --candidate-dir)
            (($# >= 2)) || { usage >&2; exit 64; }
            CANDIDATE_DIR=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            printf 'ERROR: unknown argument: %s\n' "$1" >&2
            usage >&2
            exit 64
            ;;
    esac
done

refuse_root_execution() {
    local effective_uid=${1:-$EUID}
    if [[ $effective_uid == 0 ]]; then
        printf 'ERROR: run this collector as an ordinary user; it refuses root execution. Collect additional root-only read evidence separately under authorized administration.\n' >&2
        return 77
    fi
    return 0
}
if refuse_root_execution "$EUID"; then :; else exit $?; fi

if [[ -z $OUTPUT_PARENT || $OUTPUT_PARENT != /* || ! -d $OUTPUT_PARENT || -L $OUTPUT_PARENT ]]; then
    printf 'ERROR: --output-parent must be an existing absolute non-symlink directory. No collection was started.\n' >&2
    exit 64
fi
canonical_parent=$(realpath -e -- "$OUTPUT_PARENT" 2>/dev/null) || {
    printf 'ERROR: cannot canonicalize output parent. No collection was started.\n' >&2
    exit 64
}
if [[ $canonical_parent != "$OUTPUT_PARENT" ]]; then
    printf 'ERROR: output parent must already be canonical and contain no symlink components. No collection was started.\n' >&2
    exit 64
fi
parent_uid=$(stat -c '%u' -- "$OUTPUT_PARENT" 2>/dev/null) || exit 64
parent_mode=$(stat -c '%a' -- "$OUTPUT_PARENT" 2>/dev/null) || exit 64
current_uid=$(id -u) || exit 64
if [[ $parent_uid != "$current_uid" ]] || (( (8#$parent_mode & 0022) != 0 )); then
    printf 'ERROR: output parent must be owned by the current UID and not group/world writable. No collection was started.\n' >&2
    exit 64
fi

validate_candidate_directory() {
    local candidate=$1 canonical
    if [[ -z $candidate || $candidate != /* || ! -d $candidate || -L $candidate ]]; then
        printf 'ERROR: --candidate-dir must name an existing absolute non-symlink directory. No collection was started.\n' >&2
        return 64
    fi
    canonical=$(realpath -e -- "$candidate" 2>/dev/null) || {
        printf 'ERROR: cannot canonicalize candidate directory. No collection was started.\n' >&2
        return 64
    }
    if [[ $canonical != "$candidate" ]]; then
        printf 'ERROR: candidate directory must be canonical and contain no symlink components. No collection was started.\n' >&2
        return 64
    fi
    CANDIDATE_DIR=$canonical
}

if validate_candidate_directory "$CANDIDATE_DIR"; then :; else exit $?; fi

candidate_ctest_helper_path() {
    printf '%s/alpha-m1-ctest-inventory.py' "$CANDIDATE_DIR"
}
utc_stamp=$(date -u '+%Y%m%dT%H%M%SZ') || exit 64
OUT="$OUTPUT_PARENT/alpha-m1-vm-evidence-$utc_stamp-$$"
if ! mkdir -m 700 -- "$OUT" 2>/dev/null; then
    printf 'ERROR: could not create a new unique evidence directory; nothing was overwritten. No collection was started.\n' >&2
    exit 73
fi
readonly OUT CANDIDATE_DIR
readonly REPORT="$OUT/report.md"
readonly CHECKS="$OUT/checks.tsv"

record() {
    local name=$1 status=$2 evidence=$3 detail=$4
    printf '%s\t%s\t%s\t%s\n' "$name" "$status" "$evidence" "${detail//$'\t'/ }" >>"$CHECKS"
}

printf 'check\tstatus\tevidence\tdetail\n' >"$CHECKS"
printf '# Alpha M1 VM evidence capture\n\n' >"$REPORT"
printf '%s\n' '- Overall: **BLOCKED pending human review**' '- Direct evidence means a command completed in this execution environment. It does not authenticate a hypervisor snapshot.' '- PASS/FAIL describe observed checks; BLOCKED means evidence could not be collected or verified; NOT RUN means the action was deliberately omitted.' '' >>"$REPORT"
printf '%s\n' 'No privileged CTest case was executed.' 'No canary, install, rollback, cleanup, service mutation, cgroup mutation, or mount operation was executed.' >"$OUT/not-run-actions.txt"
record privileged-ctest-execution 'NOT RUN' "$OUT/not-run-actions.txt" 'all 18 privileged cases deliberately not executed'
record canary-execution 'NOT RUN' "$OUT/not-run-actions.txt" 'live canary deliberately not executed'
record install-execution 'NOT RUN' "$OUT/not-run-actions.txt" 'administrative installation deliberately not executed'
record rollback-execution 'NOT RUN' "$OUT/not-run-actions.txt" 'rollback deliberately not executed'
record cleanup-execution 'NOT RUN' "$OUT/not-run-actions.txt" 'no cleanup performed'
record service-or-cgroup-mutation 'NOT RUN' "$OUT/not-run-actions.txt" 'no systemd or cgroup mutation performed'
record mount-operation 'NOT RUN' "$OUT/not-run-actions.txt" 'no mount operation performed'

write_report_header() {
    local report_file=$1 output_dir=$2 collector_path=$3 capture_time=$4
    printf '%s\n' "- Output directory: $output_dir" "- Collector path: $collector_path" '- Collector SHA-256 is in collector-sha256.txt.' "- Capture started UTC: $capture_time" '' >>"$report_file"
}

capture() {
    local name=$1 shift_status=${2:-PASS}
    shift 2
    local file="$OUT/$name.txt" rc command_text
    printf -v command_text '%q ' "$@"
    if ! command -v -- "$1" >/dev/null 2>&1; then
        printf 'BLOCKED: required command not found: %s\n' "$1" >"$file"
        record "$name" BLOCKED "$file" "command unavailable: $1"
        return 0
    fi
    if command -v timeout >/dev/null 2>&1; then
        if timeout --signal=TERM --kill-after=2s 30s "$@" >"$file" 2>&1; then rc=0; else rc=$?; fi
    else
        printf 'BLOCKED: timeout utility unavailable; command not started.\n' >"$file"
        record "$name" BLOCKED "$file" 'timeout utility unavailable'
        return 0
    fi
    if ((rc == 0)); then
        record "$name" "$shift_status" "$file" "exit=0 command=$command_text"
    elif ((rc == 124 || rc == 137)); then
        record "$name" BLOCKED "$file" "timed out or was terminated; exit=$rc command=$command_text"
    else
        record "$name" BLOCKED "$file" "exit=$rc command=$command_text"
    fi
}

capture_group() {
    local name=$1
    shift
    local file="$OUT/$name.txt" rc command_text
    printf -v command_text '%q ' "$@"
    if command -v timeout >/dev/null 2>&1; then
        if timeout --signal=TERM --kill-after=2s 30s "$@" >"$file" 2>&1; then rc=0; else rc=$?; fi
    else
        printf 'BLOCKED: timeout utility unavailable; command not started.\n' >"$file"
        record "$name" BLOCKED "$file" 'timeout utility unavailable'
        return 0
    fi
    if ((rc == 0)); then record "$name" PASS "$file" "exit=0 command=$command_text"
    else record "$name" BLOCKED "$file" "exit=$rc command=$command_text"; fi
}

# Parse machine-readable systemd JSON captured by the collector. Keep this as a
# production function so tests can exercise the exact parser without invoking
# systemd or running the collector against the host.
parse_systemd_unit_json() {
    local units_json=$1 unit_files_json=$2 output_file=$3
    python3 -I - "$units_json" "$unit_files_json" "$output_file" <<'PY'
import json
import os
import re
import sys

units_path, files_path, output_path = sys.argv[1:]
prefix = re.compile(r'^athena-alpha-m1-[A-Za-z0-9_.@-]+\.service$')

def read_array(path, label):
    flags = os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_CLOEXEC', 0) | getattr(os, 'O_NONBLOCK', 0)
    fd = os.open(path, flags)
    with os.fdopen(fd, 'r', encoding='utf-8') as stream:
        value = json.load(stream)
    if not isinstance(value, list):
        raise ValueError('%s JSON root must be an array' % label)
    return value

try:
    unit_rows = read_array(units_path, 'list-units')
    file_rows = read_array(files_path, 'list-unit-files')
    units = {}
    files = {}
    seen_units = set()
    seen_files = set()
    for row in unit_rows:
        if not isinstance(row, dict):
            raise ValueError('list-units contains a non-object row')
        name = row.get('unit')
        if not isinstance(name, str) or not name:
            raise ValueError('list-units row has no unit name')
        values = (row.get('load'), row.get('active'), row.get('sub'))
        if not all(isinstance(value, str) and value for value in values):
            raise ValueError('list-units row has incomplete state fields')
        if name in seen_units:
            raise ValueError('duplicate unit: ' + name)
        seen_units.add(name)
        if name.startswith('athena-alpha-m1-'):
            if not prefix.fullmatch(name):
                raise ValueError('unexpected alpha unit name')
            if values[0] not in ('loaded', 'not-found', 'error', 'masked', 'bad-setting', 'merged', 'stub'):
                raise ValueError('unexpected LoadState for ' + name)
            if values[1] not in ('active', 'inactive', 'failed', 'activating', 'deactivating', 'reloading', 'maintenance'):
                raise ValueError('unexpected ActiveState for ' + name)
            units[name] = values
    for row in file_rows:
        if not isinstance(row, dict):
            raise ValueError('list-unit-files contains a non-object row')
        name = row.get('unit_file')
        state = row.get('state')
        if not isinstance(name, str) or not name or not isinstance(state, str) or not state:
            raise ValueError('list-unit-files row has incomplete name/state')
        if name in seen_files:
            raise ValueError('duplicate unit-file: ' + name)
        seen_files.add(name)
        if name.startswith('athena-alpha-m1-'):
            if not prefix.fullmatch(name):
                raise ValueError('unexpected unit-file name')
            files[name] = state
    rows = []
    for name in sorted(set(units) | set(files)):
        if name not in units:
            classification, load, active, sub = 'not-loaded', 'unavailable', 'unavailable', 'unavailable'
        else:
            load, active, sub = units[name]
            if load != 'loaded':
                classification = 'not-loaded'
            elif active == 'failed':
                classification = 'failed'
            elif active == 'active':
                classification = 'active'
            elif active == 'inactive':
                classification = 'inactive'
            else:
                classification = active
        rows.append((name, classification, load, active, sub, files.get(name, 'unavailable')))
    with open(output_path, 'w', encoding='utf-8') as output:
        output.write('unit\tclassification\tload_state\tactive_state\tsub_state\tunit_file_state\n')
        for row in rows:
            output.write('\t'.join(row) + '\n')
except (OSError, ValueError, json.JSONDecodeError) as exc:
    print('BLOCKED: invalid or unavailable systemd JSON: %s' % exc, file=sys.stderr)
    raise SystemExit(2)
PY
}

# Reconcile current unit IDs with Session 35 and audit every process cgroup for
# stale/unaccounted Alpha service paths. This is deliberately conservative:
# unreadable process records leave the result BLOCKED; a process in an Alpha
# cgroup absent from the current inventory is a FAIL requiring investigation.
audit_alpha_process_cgroups() {
    local process_file=$1 current_units_file=$2 historical_text=$3 output_file=$4
    python3 -I - "$process_file" "$current_units_file" "$historical_text" "$output_file" <<'PY'
import os
import re
import sys

process_path, units_path, historical_text, output_path = sys.argv[1:]
name_pattern = re.compile(r'^athena-alpha-m1-[A-Za-z0-9_.@-]+\.service$')
alpha_segment = re.compile(r'(?:^|/)(athena-alpha-m1-[^/]+)')

try:
    with open(units_path, encoding='utf-8') as stream:
        current = {line.rstrip('\n') for line in stream if line.rstrip('\n')}
    historical = set(historical_text.splitlines())
    if len(historical) != 15 or any(not name_pattern.fullmatch(name) for name in historical):
        raise ValueError('fixed Session 35 unit list is malformed')
    if any(not name_pattern.fullmatch(name) for name in current):
        raise ValueError('current unit inventory contains an unexpected name')
    current_delta = sorted(current ^ historical)
    stale = []
    unparseable = []
    with open(process_path, encoding='utf-8', errors='replace') as stream:
        for line in stream:
            if ' BLOCKED=' in line:
                unparseable.append(line.strip())
                continue
            if not line.startswith('pid='):
                unparseable.append('unrecognized process record: ' + line.strip())
                continue
            match = re.search(r'(?:^|\s)cgroup=(.*?) cmd=', line)
            if not match:
                unparseable.append('process cgroup field missing: ' + line.strip())
                continue
            for membership in match.group(1).split(';'):
                for found in alpha_segment.finditer(membership):
                    name = found.group(1)
                    if not name_pattern.fullmatch(name):
                        unparseable.append('malformed Alpha cgroup component: ' + name)
                    elif name not in current:
                        stale.append((line.split(None, 1)[0], name))
    with open(output_path, 'w', encoding='utf-8') as output:
        output.write('historical_unit_count=%d\ncurrent_unit_count=%d\n' % (len(historical), len(current)))
        output.write('current_id_set_matches_session35=%s\n' % ('yes' if not current_delta else 'no'))
        for name in current_delta:
            output.write('unit_id_difference=%s\n' % name)
        for process, name in stale:
            output.write('unaccounted_alpha_cgroup_process=%s unit=%s\n' % (process, name))
        for issue in unparseable:
            output.write('unresolved_process_record=%s\n' % issue)
        if stale:
            status = 'FAIL'
        elif unparseable or current_delta:
            status = 'BLOCKED'
        else:
            status = 'PASS'
        output.write('status=%s\n' % status)
    raise SystemExit({'PASS': 0, 'FAIL': 1, 'BLOCKED': 2}[status])
except Exception as exc:
    with open(output_path, 'w', encoding='utf-8') as output:
        output.write('status=BLOCKED\nreason=%s\n' % exc)
    raise SystemExit(2)
PY
}

# Safely load, hash, and execute one exact helper byte buffer. The pathname is
# opened once with O_NOFOLLOW; the code executed below is the same buffer whose
# digest is checked. No Python interpreter is launched on the helper path.
run_pinned_ctest_inventory_helper() {
    timeout --signal=TERM --kill-after=2s 30s python3 -I - "$@" <<'PY'
import hashlib
import os
import stat
import sys

helper_path, expected_digest, build_path, manifest_path, digest_path = sys.argv[1:]
try:
    flags = os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK
    fd = os.open(helper_path, flags)
    try:
        before = os.fstat(fd)
        if not stat.S_ISREG(before.st_mode):
            raise OSError('helper is not a regular file')
        if before.st_nlink != 1:
            raise OSError('helper must have exactly one hard link')
        if before.st_size > 1024 * 1024:
            raise OSError('helper exceeds size limit')
        chunks = []
        total = 0
        while True:
            chunk = os.read(fd, min(65536, 1024 * 1024 + 1 - total))
            if not chunk:
                break
            chunks.append(chunk)
            total += len(chunk)
            if total > 1024 * 1024:
                raise OSError('helper exceeds size limit')
        after = os.fstat(fd)
        if (before.st_dev, before.st_ino, before.st_mode, before.st_nlink, before.st_size,
            before.st_mtime_ns, before.st_ctime_ns) != (
            after.st_dev, after.st_ino, after.st_mode, after.st_nlink, after.st_size,
            after.st_mtime_ns, after.st_ctime_ns
        ) or total != before.st_size:
            raise OSError('helper changed while being read')
    finally:
        os.close(fd)
    source = b''.join(chunks)
    observed = hashlib.sha256(source).hexdigest()
    with open(digest_path, 'w', encoding='ascii') as stream:
        stream.write('expected=%s\nobserved=%s\n' % (expected_digest, observed))
    if len(expected_digest) != 64 or observed != expected_digest:
        raise ValueError('reviewed helper SHA-256 mismatch')
    sys.argv = [helper_path, '--build-dir', build_path, '--manifest', manifest_path]
    namespace = {'__name__': '__main__', '__file__': helper_path}
    exec(compile(source, helper_path, 'exec'), namespace, namespace)
except BaseException as exc:
    if isinstance(exc, SystemExit):
        raise
    print('BLOCKED: cannot verify/execute inventory helper: %s' % exc, file=sys.stderr)
    raise SystemExit(2)
PY
}

check_path_components() {
    local path=$1 output_file=$2
    if ! command -v python3 >/dev/null 2>&1; then
        printf 'BLOCKED: python3 unavailable; path components were not checked.\n' >"$output_file"
        return 127
    fi
    if python3 -I - "$path" >"$output_file" 2>&1 <<'PY'
import os, stat, sys
path = sys.argv[1]
if not os.path.isabs(path):
    print('BLOCKED: path is not absolute')
    raise SystemExit(3)
parts = [part for part in path.split('/') if part]
current = '/'
try:
    for index, part in enumerate(parts):
        if part in ('.', '..'):
            print('BLOCKED: dot path component')
            raise SystemExit(3)
        current = os.path.join(current, part)
        info = os.lstat(current)
        if stat.S_ISLNK(info.st_mode):
            print('BLOCKED: symlink component: ' + current)
            raise SystemExit(3)
        if index < len(parts) - 1 and not stat.S_ISDIR(info.st_mode):
            print('BLOCKED: non-directory ancestor: ' + current)
            raise SystemExit(3)
    print('PASS: no symlink or non-directory ancestor: ' + path)
except OSError as exc:
    print('BLOCKED: cannot inspect path component %s: %s' % (current, exc))
    raise SystemExit(2)
PY
    then
        return 0
    else
        return $?
    fi
}

capture_path() {
    local label=$1 path=$2 safe_label=$3
    local components_file="$OUT/path-$safe_label-components.txt" component_rc
    if check_path_components "$path" "$components_file"; then
        record "path-$safe_label-components" PASS "$components_file" 'direct lstat traversal found no symlink components'
    else
        component_rc=$?
        record "path-$safe_label-components" BLOCKED "$components_file" "safe component inspection failed with exit=$component_rc; target not queried"
        return 0
    fi
    capture "path-$safe_label-stat" PASS stat -c 'type=%F mode=%a uid=%u gid=%g links=%h path=%n' -- "$path"
    capture "path-$safe_label-namei" PASS namei -l -- "$path"
    capture "path-$safe_label-acl" PASS getfacl -cp -- "$path"
    capture "path-$safe_label-mount" PASS findmnt -T "$path" -o TARGET,SOURCE,FSTYPE,OPTIONS
    capture "path-$safe_label-space" PASS df -P "$path"
    if [[ -d $path && ! -L $path ]]; then
        capture_group "path-$safe_label-children" find -P "$path" -mindepth 1 -maxdepth 3 -printf '%y mode=%m uid:gid=%U:%G links=%n %p\n'
    fi
    if [[ $path == "$RECEIPT" && -f $path && ! -L $path ]]; then
        capture path-receipt-sha256 PASS sha256sum -- "$path"
    fi
    : "$label"
}

on_signal() {
    local signal=$1
    printf '\n- Collection interrupted by %s; partial evidence is retained in this new directory.\n' "$signal" >>"$REPORT"
    record interruption BLOCKED "$REPORT" "received $signal; no cleanup performed"
    exit 128
}
trap 'on_signal HUP' HUP
trap 'on_signal INT' INT
trap 'on_signal TERM' TERM

capture_started_utc=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
collector_source_label=$0
if [[ $0 =~ ^/proc/self/fd/[0-9]+$ ]]; then
    collector_source_label="sealed memfd via $0"
fi
write_report_header "$REPORT" "$OUT" "$collector_source_label" "$capture_started_utc"
if [[ $0 =~ ^/proc/self/fd/[0-9]+$ && -r $0 ]]; then
    capture collector-sha256 PASS sha256sum -- "$0"
elif [[ -f $0 && ! -L $0 ]]; then
    capture collector-sha256 PASS sha256sum -- "$0"
else
    record collector-sha256 BLOCKED '' 'collector source is neither the expected inherited memfd locator nor a regular non-symlink file'
fi
capture vm-time PASS date -u '+%Y-%m-%dT%H:%M:%SZ'
capture vm-hostname PASS hostname
capture vm-user PASS id
capture vm-os PASS cat /etc/os-release
capture vm-kernel PASS uname -a
capture vm-architecture PASS uname -m
capture vm-virtualization PASS systemd-detect-virt
virt_result=$(head -n 1 "$OUT/vm-virtualization.txt" 2>/dev/null || true)
case "$virt_result" in
    container|container-*|docker|docker-*|podman|podman-*|lxc|lxc-*|systemd-nspawn|openvz|rkt|rkt-*)
        record vm-environment FAIL "$OUT/vm-virtualization.txt" "container environment reported: $virt_result" ;;
    '')
        record vm-environment BLOCKED "$OUT/vm-virtualization.txt" 'virtualization detection returned no usable value' ;;
    *)
        record vm-environment PASS "$OUT/vm-virtualization.txt" "direct virtualization observation: $virt_result; independently confirm the approved VM identity" ;;
esac
capture vm-machine-id PASS cat /etc/machine-id
capture vm-boot-id PASS cat /proc/sys/kernel/random/boot_id
capture vm-systemd-version PASS systemctl --version
capture vm-systemd-health PASS systemctl is-system-running
printf 'Snapshot ID: NOT OBSERVED by this script; record independently from the authorized VM/hypervisor interface.\n' >"$OUT/vm-snapshot.txt"
record vm-snapshot BLOCKED "$OUT/vm-snapshot.txt" 'not queried; no authorized hypervisor interface is assumed'

capture systemd-alpha-units PASS systemctl list-units --all --type=service --output=json --no-pager
capture systemd-alpha-unit-files PASS systemctl list-unit-files --type=service --output=json --no-pager
units_file="$OUT/alpha-unit-names.txt"
states_file="$OUT/alpha-unit-states.tsv"
if parse_systemd_unit_json "$OUT/systemd-alpha-units.txt" "$OUT/systemd-alpha-unit-files.txt" "$states_file"; then
    awk -F '\t' 'NR > 1 {print $1}' "$states_file" >"$units_file"
    failed_units_file="$OUT/alpha-failed-unit-names.txt"
    awk -F '\t' 'NR > 1 && $2 == "failed" {print $1}' "$states_file" >"$failed_units_file"
    unit_count=$(($(wc -l <"$units_file")))
    failed_unit_count=$(($(wc -l <"$failed_units_file")))
    if ((unit_count == 15)); then record alpha-unit-count PASS "$states_file" 'parsed 15 alpha service units from systemd JSON'
    elif ((unit_count > 0)); then record alpha-unit-count FAIL "$states_file" "expected previously observed count 15; observed $unit_count"
    else record alpha-unit-count BLOCKED "$states_file" 'no alpha service units were present in either systemd JSON inventory'; fi
    if ((failed_unit_count == 15)); then record alpha-failed-unit-count PASS "$failed_units_file" 'observed 15 matching failed units from structured ActiveState values'
    elif ((failed_unit_count > 0)); then record alpha-failed-unit-count FAIL "$failed_units_file" "expected previously observed count 15; observed $failed_unit_count"
    else record alpha-failed-unit-count BLOCKED "$failed_units_file" 'no failed alpha service units parsed; inspect structured systemd inventory'; fi
    for state in failed active inactive not-loaded activating deactivating reloading maintenance; do
        count=$(awk -F '\t' -v state="$state" 'NR > 1 && $2 == state {n++} END {print n+0}' "$states_file")
        record "alpha-unit-state-$state" PASS "$states_file" "observed $count units classified $state"
    done
else
    printf 'BLOCKED: systemd JSON could not be parsed or was incomplete; unit counts are unavailable.\n' >"$units_file"
    printf 'BLOCKED: failed-unit count unavailable because systemd JSON was not verified.\n' >"$OUT/alpha-failed-unit-names.txt"
    unit_count=0
    failed_unit_count=0
    record alpha-unit-inventory BLOCKED "$units_file" 'systemctl failed, returned malformed/incomplete JSON, or exposed duplicate/unexpected unit names'
    record alpha-unit-count BLOCKED "$units_file" 'structured systemd inventory unavailable'
    record alpha-failed-unit-count BLOCKED "$OUT/alpha-failed-unit-names.txt" 'structured systemd inventory unavailable'
fi

while IFS= read -r unit; do
    [[ $unit =~ ^athena-alpha-m1-[A-Za-z0-9_.@-]+\.service$ ]] || continue
    capture "unit-${unit}.show" PASS systemctl show "$unit" --property=Id,LoadState,ActiveState,SubState,MainPID,ControlGroup,Result,ExecMainCode,ExecMainStatus
    capture "unit-${unit}.journal" PASS journalctl --no-pager -n 100 -o short-iso-precise -u "$unit"
    control_group=$(sed -n 's/^ControlGroup=//p' "$OUT/unit-${unit}.show.txt" | head -n 1)
    if [[ -z $control_group ]]; then
        printf 'BLOCKED: unit has empty or unreadable ControlGroup; do not infer absence of descendants.\n' >"$OUT/unit-${unit}.cgroup-events.txt"
        record "unit-${unit}.cgroup-events" BLOCKED "$OUT/unit-${unit}.cgroup-events.txt" 'empty/unavailable ControlGroup'
    elif [[ $control_group =~ ^/system\.slice/athena-alpha-m1-[A-Za-z0-9_.@-]+\.service$ ]]; then
        cgroup_dir="/sys/fs/cgroup$control_group"
        cgroup_events="$cgroup_dir/cgroup.events"
        if [[ -d $cgroup_dir && ! -L $cgroup_dir && -f $cgroup_events && ! -L $cgroup_events && -r $cgroup_events ]] && check_path_components "$cgroup_events" "$OUT/unit-${unit}.cgroup-path-components.txt"; then
            capture "unit-${unit}.cgroup-events" PASS cat -- "$cgroup_events"
        else
            printf 'BLOCKED: expected cgroup directory/events unavailable or symlinked: %s\n' "$cgroup_events" >"$OUT/unit-${unit}.cgroup-events.txt"
            record "unit-${unit}.cgroup-events" BLOCKED "$OUT/unit-${unit}.cgroup-events.txt" 'cgroup events could not be safely read'
        fi
    else
        printf 'BLOCKED: unexpected ControlGroup value: %s\n' "$control_group" >"$OUT/unit-${unit}.cgroup-events.txt"
        record "unit-${unit}.cgroup-events" BLOCKED "$OUT/unit-${unit}.cgroup-events.txt" 'ControlGroup did not match expected system.slice form'
    fi
done <"$units_file"

capture system-process-list PASS ps -eo pid=,ppid=,uid=,stat=,comm=,args= --sort=pid
capture_group system-process-cgroups python3 -I - <<'PY'
import os
for name in sorted((n for n in os.listdir('/proc') if n.isdigit()), key=int):
    base = '/proc/' + name
    try:
        with open(base + '/status', encoding='utf-8', errors='replace') as stream:
            status = {}
            for line in stream:
                if ':' in line:
                    key, value = line.split(':', 1)
                    status[key] = value.strip()
        with open(base + '/cgroup', encoding='utf-8', errors='replace') as stream:
            groups = ';'.join(line.strip() for line in stream)
        with open(base + '/cmdline', 'rb') as stream:
            cmd = stream.read(65536).replace(b'\0', b' ').decode('utf-8', 'replace').strip()
        print('pid=%s ppid=%s uid=%s state=%s cgroup=%s cmd=%s' % (
            name, status.get('PPid', '?'), status.get('Uid', '?').split()[0],
            status.get('State', '?'), groups, cmd or '[' + status.get('Name', '?') + ']'))
    except (OSError, PermissionError, ProcessLookupError) as exc:
        print('pid=%s BLOCKED=%s' % (name, type(exc).__name__))
PY
capture proc-cgroup-v2-root PASS cat /sys/fs/cgroup/cgroup.events
process_audit_file="$OUT/alpha-process-cgroup-audit.txt"
if audit_alpha_process_cgroups "$OUT/system-process-cgroups.txt" "$units_file" "$SESSION35_ALPHA_UNITS" "$process_audit_file"; then process_audit_rc=0; else process_audit_rc=$?; fi
case "$process_audit_rc" in
    0) record alpha-process-cgroup-audit PASS "$process_audit_file" 'all process cgroups parsed; current service IDs match Session 35 and no stale Alpha cgroup process was found' ;;
    1) record alpha-process-cgroup-audit FAIL "$process_audit_file" 'process remains in an Alpha cgroup absent from the current service inventory' ;;
    *) record alpha-process-cgroup-audit BLOCKED "$process_audit_file" 'process cgroups or current-vs-historical unit identity could not be fully reconciled' ;;
esac
if grep -q '^current_id_set_matches_session35=yes$' "$process_audit_file"; then
    record alpha-session35-unit-id-reconciliation PASS "$process_audit_file" 'current Alpha service ID set matches fixed Session 35 inventory'
else
    record alpha-session35-unit-id-reconciliation BLOCKED "$process_audit_file" 'current Alpha service ID set differs from Session 35 or inventory unavailable'
fi
residual_gate=PASS
residual_reason='all observed unit cgroups had populated=0 and no process snapshot entry referenced them'
if ((unit_count != 15 || failed_unit_count != 15)); then
    residual_gate=BLOCKED
    residual_reason="residual proof requires 15 enumerated and 15 failed units; observed units=$unit_count failed=$failed_unit_count"
fi
if ! grep -q 'cgroup=.*0::/' "$OUT/system-process-cgroups.txt" 2>/dev/null; then
    residual_gate=BLOCKED
    residual_reason='unified cgroup process membership unavailable; residual absence cannot be assessed'
fi
if grep -q ' BLOCKED=' "$OUT/system-process-cgroups.txt" 2>/dev/null; then
    residual_gate=BLOCKED
    residual_reason='one or more process cgroup records could not be read'
fi
for unit in $(cat "$units_file"); do
    [[ $unit =~ ^athena-alpha-m1-[A-Za-z0-9_.@-]+\.service$ ]] || continue
    show_file="$OUT/unit-${unit}.show.txt"
    events_file="$OUT/unit-${unit}.cgroup-events.txt"
    cg=$(sed -n 's/^ControlGroup=//p' "$show_file" 2>/dev/null | head -n 1)
    if [[ -z $cg ]]; then
        residual_gate=BLOCKED
        residual_reason="empty or unavailable ControlGroup for $unit"
        continue
    fi
    if grep -q '^populated 1$' "$events_file" 2>/dev/null; then
        residual_gate=FAIL
        residual_reason="cgroup reports populated=1 for $unit"
    elif ! grep -q '^populated 0$' "$events_file" 2>/dev/null; then
        [[ $residual_gate == FAIL ]] || residual_gate=BLOCKED
        residual_reason="cgroup populated state unavailable for $unit"
    fi
    if awk -v cg="$cg" 'index($0, "cgroup=0::" cg ";") || index($0, "cgroup=0::" cg " ") || index($0, "cgroup=0::" cg "\n") { found=1 } END { exit !found }' "$OUT/system-process-cgroups.txt"; then
        residual_gate=FAIL
        residual_reason="process snapshot contains a member of $cg"
    fi
done
if ((process_audit_rc == 1)); then
    residual_gate=FAIL
    residual_reason='a process cgroup references an Alpha service absent from the current unit inventory'
elif ((process_audit_rc != 0)) && [[ $residual_gate != FAIL ]]; then
    residual_gate=BLOCKED
    residual_reason='process cgroup audit or Session 35 unit identity reconciliation is incomplete'
fi
record residual-process-gate "$residual_gate" "$OUT/system-process-cgroups.txt" "$residual_reason"

for item in \
    'systemd-run-dir|/run/systemd/system' \
    'pid1-cgroup|/proc/1/cgroup' \
    'state-root|/var/lib/athena-alpha-m1' \
    'fixture|/var/lib/athena-alpha-m1/ext4-fixture' \
    'receipt|/var/lib/athena-alpha-m1/systemd-canary.json' \
    'backups|/var/lib/athena-alpha-m1/backups' \
    'log-root|/var/log/athena-alpha-m1' \
    'd6-parent|/var/tmp/athena-d67-ext4' \
    'd6-residual|/var/tmp/athena-d67-ext4/athena-d6-8yRDjc' \
    'protected-build|/opt/athena-qualification/build' \
    'isolated-build|/tmp/monad-s32-verification-final4-20261021'; do
    safe_label=${item%%|*}
    path=${item#*|}
    capture_path "$safe_label" "$path" "$safe_label"
done

for archive_root in "$BACKUPS" "$LOG_ROOT"; do
    label=backups
    [[ $archive_root == "$LOG_ROOT" ]] && label=logs
    if [[ -d $archive_root && ! -L $archive_root ]] && check_path_components "$archive_root" "$OUT/$label-root-components.txt"; then
        capture_group "$label-evidence-file-acls" find -P "$archive_root" -maxdepth 4 -type f \( -name 'transaction.json' -o -name 'backup-manifest.json' -o -name '*.receipt' -o -name '*receipt*.json' -o -name 'systemd-canary.json' \) -exec getfacl -cp -- '{}' +
        capture_group "$label-evidence-file-sha256" find -P "$archive_root" -maxdepth 4 -type f \( -name 'transaction.json' -o -name 'backup-manifest.json' -o -name '*.receipt' -o -name '*receipt*.json' -o -name 'systemd-canary.json' \) -exec sha256sum -- '{}' +
    else
        printf 'BLOCKED: directory absent, inaccessible, or symlinked; not traversed: %s\n' "$archive_root" >"$OUT/$label-evidence-file-sha256.txt"
        record "$label-evidence-file-sha256" BLOCKED "$OUT/$label-evidence-file-sha256.txt" 'root not safely readable'
    fi
done

candidate_files=(
    'installer|install-athena-alpha-m1-admin.sh'
    'manifest|alpha-m1-build-reconciliation-session34b-candidate.json'
    'runner|athena-alpha-m1-runner.py'
    'support|gateway_support.py'
    'canary|athena-alpha-m1-systemd-canary.py'
    'sudoers|athena-alpha-m1-sudoers'
)
expected_hashes=("$BASELINE_INSTALLER" "$BASELINE_MANIFEST" "$BASELINE_RUNNER" "$BASELINE_SUPPORT" "$BASELINE_CANARY" "$BASELINE_SUDOERS")
index=0
manifest_hash_matches=false
for item in "${candidate_files[@]}"; do
    label=${item%%|*}
    filename=${item#*|}
    path="$CANDIDATE_DIR/$filename"
    evidence="$OUT/candidate-$label-sha256.txt"
    expected=${expected_hashes[$index]}
    if [[ -f $path && ! -L $path ]]; then
        if observed=$(timeout --signal=TERM --kill-after=2s 30s python3 -I - "$path" <<'PY'
import hashlib, os, stat, sys
path = sys.argv[1]
try:
    fd = os.open(path, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_CLOEXEC', 0) | getattr(os, 'O_NONBLOCK', 0))
    if not stat.S_ISREG(os.fstat(fd).st_mode):
        os.close(fd)
        raise OSError('not a regular file')
    digest = hashlib.sha256()
    with os.fdopen(fd, 'rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    print(digest.hexdigest())
except OSError as exc:
    print('BLOCKED: %s' % exc)
    raise SystemExit(2)
PY
        ); then rc=0; else rc=$?; fi
        actual=$observed
        printf 'expected=%s\nobserved=%s\n' "$expected" "$actual" >"$evidence"
        if [[ $label == manifest && $rc == 0 && $actual == "$expected" ]]; then manifest_hash_matches=true; fi
        if ((rc == 0)) && [[ $actual == "$expected" ]]; then record "candidate-$label" PASS "$evidence" 'direct hash matches fixed Session 34B baseline'
        elif ((rc == 0)); then record "candidate-$label" FAIL "$evidence" 'hash differs from fixed Session 34B baseline'
        else record "candidate-$label" BLOCKED "$evidence" 'hash command failed'; fi
    else
        printf 'BLOCKED: expected regular non-symlink candidate file missing: %s\n' "$path" >"$evidence"
        record "candidate-$label" BLOCKED "$evidence" 'candidate file absent, inaccessible, or symlinked'
    fi
    ((index += 1))
done

manifest_path="$CANDIDATE_DIR/alpha-m1-build-reconciliation-session34b-candidate.json"
if [[ $manifest_hash_matches == true && -f $manifest_path && ! -L $manifest_path ]] && command -v python3 >/dev/null 2>&1; then
    for build in "$PROTECTED_BUILD" "$ISOLATED_BUILD"; do
        build_tag=protected
        [[ $build == "$ISOLATED_BUILD" ]] && build_tag=isolated
        safe_build=true
        for required_path in "$build" "$build/CMakeCache.txt" "$build/build.ninja" "$build/CTestTestfile.cmake"; do
            component_file="$OUT/build-$build_tag-path-components.txt"
            if [[ ! -e $required_path || -L $required_path || ( $required_path != "$build" && ! -f $required_path ) ]] || ! check_path_components "$required_path" "$component_file"; then safe_build=false; break; fi
        done
        if [[ $safe_build == true ]]; then
            symlink_scan="$OUT/build-$build_tag-symlinks.txt"
            if symlink_output=$(timeout --signal=TERM --kill-after=2s 30s find -P "$build" -type l -print -quit 2>&1); then
                printf '%s\n' "${symlink_output:-NO_SYMLINKS_FOUND}" >"$symlink_scan"
                if [[ -n $symlink_output ]]; then safe_build=false; record "build-$build_tag-symlinks" BLOCKED "$symlink_scan" 'build tree contains symlink; CTest will not run'
                else record "build-$build_tag-symlinks" PASS "$symlink_scan" 'no symlink found in build tree'; fi
            else
                printf '%s\n' "$symlink_output" >"$symlink_scan"
                safe_build=false
                record "build-$build_tag-symlinks" BLOCKED "$symlink_scan" 'could not complete no-follow symlink scan; CTest will not run'
            fi
        fi
        if [[ $safe_build == true ]]; then
            capture "build-$build_tag-cmake-cache" PASS cat -- "$build/CMakeCache.txt"
            capture "build-$build_tag-generated-files" PASS sha256sum -- "$build/build.ninja" "$build/CTestTestfile.cmake"
        else
            printf 'BLOCKED: build directory or required CMake/CTest file absent or symlinked; generated inventory was not read.\n' >"$OUT/build-$build_tag-ctest-inventory.txt"
            record "build-$build_tag-inspection" BLOCKED "$OUT/build-$build_tag-ctest-inventory.txt" 'symlink/missing build input; generated inventory not read'
        fi
        inventory_json="$OUT/build-$build_tag-ctest-inventory.json"
        helper_sha_file="$OUT/build-$build_tag-ctest-helper-sha256.txt"
        printf '%s\n' 'BLOCKED: helper bytes were not verified.' >"$helper_sha_file"
        if [[ $safe_build != true ]]; then
            printf '%s\n' '{"status":"BLOCKED","reason":"build safety checks failed; generated inventory not read"}' >"$inventory_json"
            record "build-$build_tag-ctest-inventory" BLOCKED "$inventory_json" 'build safety checks failed; generated inventory not read'
        elif [[ $REVIEWED_CTEST_HELPER_SHA256 =~ ^[a-f0-9]{64}$ ]]; then
            if run_pinned_ctest_inventory_helper "$(candidate_ctest_helper_path)" "$REVIEWED_CTEST_HELPER_SHA256" "$build" "$manifest_path" "$helper_sha_file" >"$inventory_json" 2>&1; then inventory_rc=0; else inventory_rc=$?; fi
            if ((inventory_rc == 0)); then
                record "build-$build_tag-ctest-inventory" PASS "$inventory_json" "generated inventory checked; no tests executed; helper SHA evidence=$helper_sha_file"
            elif ((inventory_rc == 1)); then
                record "build-$build_tag-ctest-inventory" FAIL "$inventory_json" "generated inventory parsed and differs from manifest; helper SHA evidence=$helper_sha_file"
            else
                record "build-$build_tag-ctest-inventory" BLOCKED "$inventory_json" "inventory helper unverified, unsafe, malformed, or failed (exit=$inventory_rc); helper SHA evidence=$helper_sha_file"
            fi
        else
            printf '%s\n' '{"status":"BLOCKED","reason":"reviewed CTest helper SHA-256 pin is not configured"}' >"$inventory_json"
            record "build-$build_tag-ctest-inventory" BLOCKED "$inventory_json" 'reviewed CTest helper SHA-256 pin is not configured'
        fi
        if [[ $safe_build == true ]]; then
        if timeout --signal=TERM --kill-after=2s 60s python3 -I - "$manifest_path" "$build" "$OUT/build-$build_tag-artifact-reconciliation.tsv" <<'PY'
import hashlib, json, os, stat, sys
manifest_path, build, output = sys.argv[1:]
allowed = {'activation_execution_privileged_test', 'activation_plan_retention_test',
           'activation_plan_persistence_test', 'legacy_d3_test', 'initial_activation_test',
           'libathena_core.so', 'CTestTestfile.cmake'}
try:
    fd = os.open(manifest_path, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_CLOEXEC', 0) | getattr(os, 'O_NONBLOCK', 0))
    with os.fdopen(fd, 'r', encoding='utf-8') as manifest_file:
        manifest_bytes = manifest_file.buffer.read()
    if hashlib.sha256(manifest_bytes).hexdigest() != '00fd4036bf0f3873f5c2a0e932c912b4709460fa52c35a1c2564016a92811262':
        raise ValueError('manifest bytes do not match the fixed Session 34B hash')
    manifest = json.loads(manifest_bytes)
    record = next(item for item in manifest['builds'] if item.get('path') == build)
    expected = {item['name']: item['sha256'] for item in record.get('artifacts', [])
                if item.get('name') in allowed and item.get('path') == os.path.join(build, item.get('name', ''))}
    with open(output, 'w', encoding='utf-8') as stream:
        stream.write('artifact\texpected_sha256\tobserved_sha256\tstatus\n')
        for name in sorted(allowed):
            path = os.path.join(build, name)
            want = expected.get(name, '')
            try:
                st = os.lstat(path)
                if not stat.S_ISREG(st.st_mode): raise OSError('not a regular file')
                digest = hashlib.sha256()
                fd = os.open(path, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_CLOEXEC', 0) | getattr(os, 'O_NONBLOCK', 0))
                opened = os.fstat(fd)
                if not stat.S_ISREG(opened.st_mode):
                    os.close(fd)
                    raise OSError('opened object is not a regular file')
                with os.fdopen(fd, 'rb') as source:
                    for block in iter(lambda: source.read(1024 * 1024), b''):
                        digest.update(block)
                got = digest.hexdigest()
                result = 'PASS' if want and got == want else ('FAIL' if want else 'BLOCKED')
            except OSError:
                got, result = '', 'BLOCKED'
            stream.write('%s\t%s\t%s\t%s\n' % (name, want, got, result))
    states = []
    with open(output, encoding='utf-8') as stream:
        next(stream, None)
        states = [line.rstrip('\n').split('\t')[-1] for line in stream if line.strip()]
    sys.exit(0 if states and all(value == 'PASS' for value in states) else (1 if 'FAIL' in states else 2))
except Exception as exc:
    with open(output, 'w', encoding='utf-8') as stream:
        stream.write('BLOCKED\tmanifest reconciliation error: %r\n' % (exc,))
    sys.exit(2)
PY
        then python_rc=0; else python_rc=$?; fi
        if ((python_rc == 0)); then record "build-$build_tag-artifacts" PASS "$OUT/build-$build_tag-artifact-reconciliation.tsv" 'all allowlisted artifacts matched manifest; no binary executed'
        elif ((python_rc == 1)); then record "build-$build_tag-artifacts" FAIL "$OUT/build-$build_tag-artifact-reconciliation.tsv" 'one or more observed artifact hashes differ from manifest'
        else record "build-$build_tag-artifacts" BLOCKED "$OUT/build-$build_tag-artifact-reconciliation.tsv" 'one or more artifacts or manifest entries were unavailable'; fi
        else
            printf 'BLOCKED: build safety checks failed; artifact files were not opened.\n' >"$OUT/build-$build_tag-artifact-reconciliation.tsv"
            record "build-$build_tag-artifacts" BLOCKED "$OUT/build-$build_tag-artifact-reconciliation.tsv" 'unsafe/missing build paths'
        fi
        if source_dir=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$OUT/build-$build_tag-cmake-cache.txt" 2>/dev/null | head -n 1); then :; else source_dir=''; fi
        {
            printf 'build_path=%s\ncmake_source_directory=%s\n' "$build" "${source_dir:-UNAVAILABLE}"
            if [[ -n $source_dir && -d $source_dir && ! -L $source_dir ]] && check_path_components "$source_dir" "$OUT/build-$build_tag-source-components.txt"; then
                printf '\nsource_metadata:\n'
                stat -c 'type=%F mode=%a uid=%u gid=%g path=%n' -- "$source_dir" 2>&1 || true
                if command -v git >/dev/null 2>&1; then
                    GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null GIT_OPTIONAL_LOCKS=0 git -c core.fsmonitor=false -c core.hooksPath=/dev/null -C "$source_dir" rev-parse --show-toplevel --verify HEAD 2>&1 || true
                else
                    printf 'git=NOT_AVAILABLE\n'
                fi
            else printf 'source_directory_metadata=BLOCKED\n'; fi
        } >"$OUT/build-$build_tag-provenance.txt"
        record "build-$build_tag-provenance" BLOCKED "$OUT/build-$build_tag-provenance.txt" 'source revision observation does not cryptographically prove source-to-binary provenance'
    done
else
    record build-reconciliation BLOCKED '' 'candidate manifest did not match the fixed Session 34B hash or was unavailable; build manifest parsing and comparison not attempted'
fi

capture collector-runtime-tools PASS bash --version
printf '\n- Collector finished UTC: %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" >>"$REPORT"
printf '%s\n' '- No CTest case, privileged command, canary, installer, rollback, cleanup, service mutation, mount operation, chmod, or chown was run.' '- Read `checks.tsv` and every raw evidence file. Any BLOCKED, FAIL, unexpected unit/process/cgroup, hash/inventory mismatch, permission error, or discrepancy is a stop condition.' '- Systemd process/cgroup observations do not by themselves prove descendant absence when ControlGroup or cgroup.events is missing.' '- Build cache metadata and matching file hashes do not independently authenticate source-to-binary provenance.' '- Snapshot identity must be recorded from an authorized hypervisor/VM interface; this collector does not infer it.' '' >>"$REPORT"
printf 'Evidence directory: %s\n' "$OUT"
printf 'Review report: %s\n' "$REPORT"
printf 'Collector exit 0 means collection ended; it is not a PASS or authorization. Review checks.tsv and the report.\n'
exit 0
