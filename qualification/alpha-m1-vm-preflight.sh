#!/bin/sh
# Read-only Alpha M1 VM preflight. Run interactively on the target Debian VM.
# Does not invoke sudo for privileged actions, install files, or execute tests.
set -u
export LC_ALL=C

BUILD=/opt/athena-qualification/build
RUNNER=/usr/local/sbin/athena-alpha-m1-runner
SUPPORT=/usr/local/sbin/gateway_support.py
CANARY=/usr/local/sbin/athena-alpha-m1-systemd-canary.py
SUDOERS=/etc/sudoers.d/athena-alpha-m1
FIXTURE=/var/lib/athena-alpha-m1/ext4-fixture
D6=/var/tmp/athena-d67-ext4
EXPECTED='activation_execution_privileged_sources_test
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

section() { printf '\n===== %s =====\n' "$1"; }
show_path() {
    path=$1
    printf '\n-- %s --\n' "$path"
    if [ -e "$path" ] || [ -L "$path" ]; then
        stat -c 'type=%F mode=%a uid:gid=%u:%g path=%n' -- "$path" 2>&1 || true
        if [ -f "$path" ]; then sha256sum -- "$path" 2>&1 || true; fi
    else
        printf 'MISSING\n'
    fi
}

section 'Environment / VM identity'
date -u '+utc=%Y-%m-%dT%H:%M:%SZ' 2>&1 || true
id 2>&1 || true
cat /etc/os-release 2>&1 || true
printf 'virtualization='; systemd-detect-virt 2>&1 || true
printf 'boot_id='; cat /proc/sys/kernel/random/boot_id 2>&1 || true
printf 'machine_id='; cat /etc/machine-id 2>&1 || true
printf 'snapshot_id=OPERATOR MUST RECORD FROM VM/HYPERVISOR\n'

section 'systemd health'
printf 'is-system-running: '; systemctl is-system-running 2>&1 || true
printf '%s\n' '-- failed units --'
systemctl --failed --no-pager --plain 2>&1 || true
printf '%s\n' '-- Alpha M1 unit states --'
systemctl list-units --all --no-legend --no-pager 'athena-alpha-m1-*' 2>&1 || true

section 'Installed artifact hashes and metadata'
show_path "$RUNNER"
show_path "$SUPPORT"
show_path "$CANARY"
show_path "$SUDOERS"
for path in /usr/local /usr/local/sbin /etc/sudoers.d /var/lib/athena-alpha-m1; do show_path "$path"; done

section 'sudoers read-only validation'
if [ -x /usr/sbin/visudo ]; then /usr/sbin/visudo -c 2>&1 || true; else printf 'NOT VERIFIED: /usr/sbin/visudo unavailable\n'; fi
printf 'Current account policy listing (no privileged command execution):\n'
if command -v sudo >/dev/null 2>&1; then sudo -n -l 2>&1 || true; else printf 'sudo executable unavailable\n'; fi

section 'Build and privileged CTest inventory (inventory only; no test execution)'
show_path "$BUILD"
printf '\n%s\n' '-- build artifact hashes; reconcile against the independently reviewed candidate manifest --'
for artifact in activation_execution_privileged_test activation_plan_retention_test activation_plan_persistence_test legacy_d3_test initial_activation_test libathena_core.so CTestTestfile.cmake; do
    show_path "$BUILD/$artifact"
done
if command -v ctest >/dev/null 2>&1 && [ -d "$BUILD" ]; then
    inventory=$(ctest --test-dir "$BUILD" --show-only=json-v1 -L privileged 2>&1)
    ctest_rc=$?
    printf 'ctest_exit=%s\n%s\n' "$ctest_rc" "$inventory"
    actual=$(printf '%s\n' "$inventory" | python3 -c 'import json,sys; d=json.load(sys.stdin); print("\n".join(t["name"] for t in d.get("tests", [])))' 2>&1)
    python_rc=$?
    expected=$(printf '%b' "$EXPECTED")
    if [ "$python_rc" -eq 0 ] && [ "$actual" = "$expected" ]; then
        printf 'EXACT_INVENTORY_MATCH=yes; privileged_case_count=18\n'
    else
        printf 'EXACT_INVENTORY_MATCH=no; parsed_count=%s\n' "$(printf '%s\n' "$actual" | awk 'NF {n++} END {print n+0}')"
        printf '%s\n' "$actual"
    fi
else
    printf 'NOT VERIFIED: ctest or build directory unavailable\n'
fi

section 'Filesystem mounts and fixture / D6 preservation state'
for path in "$BUILD" /tmp /var/lib/athena-alpha-m1 "$FIXTURE" "$D6"; do
    show_path "$path"
    if [ -e "$path" ]; then findmnt -T "$path" -o TARGET,SOURCE,FSTYPE,OPTIONS 2>&1 || true; fi
done
for path in "$FIXTURE" "$D6"; do
    printf '\n-- names/metadata only (read-only): %s --\n' "$path"
    if [ -d "$path" ] && [ -r "$path" ] && [ -x "$path" ]; then
        find "$path" -mindepth 1 -maxdepth 2 -printf '%y mode=%m uid:gid=%U:%G %p\n' 2>&1 || true
    else
        printf 'NOT VERIFIED: directory absent or unreadable\n'
    fi
done

section 'Canary prerequisites (indicators only; canary is NOT run)'
for tool in systemctl systemd-run findmnt python3; do command -v "$tool" 2>&1 || true; done
for path in /usr/bin/python3 /usr/bin/systemd-run /usr/bin/systemctl /usr/bin/findmnt /usr/sbin/visudo "$CANARY" "$RUNNER"; do
    if [ -x "$path" ]; then printf 'executable=yes %s\n' "$path"; else printf 'executable=no %s\n' "$path"; fi
done
printf 'canary_prerequisite_decision=REQUIRES HUMAN REVIEW OF VM IDENTITY, SYSTEMD, HASHES, MOUNTS, FIXTURE, AND CURRENT APPROVAL\n'
printf '\nPREFLIGHT ONLY: no privileged tests, canary, installation, or administrative mutation was performed. Preserve this output with UTC time and boot ID.\n'
exit 0
