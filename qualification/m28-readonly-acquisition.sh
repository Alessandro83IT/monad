#!/bin/bash
# M28 bounded metadata acquisition. Explicit operator invocation only.
set -uo pipefail
umask 077
if [[ ${1:-} == "--help" || $# -ne 1 ]]; then
  echo "Usage: sudo $0 NEW_EVIDENCE_DIRECTORY" >&2
  exit 2
fi
exec /usr/bin/python3 - "$1" <<'PY'
import datetime, hashlib, json, os, platform, stat, sys

OUT = sys.argv[1]
EXPECTED = [
    "/usr/local/sbin/athena-alpha-m1-runner",
    "/usr/local/sbin/gateway_support.py",
    "/etc/sudoers.d/athena-alpha-m1",
    "/var/log/athena-alpha-m1",
    "/var/lib/athena-alpha-m1/ext4-fixture",
    "/var/lib/athena-alpha-m1/backups",
    "/var/lib/athena-alpha-m1/systemd-canary.json",
    "/var/lib/athena-alpha-m1/admin-install.lock",
    "/var/lib/athena-alpha-m1",
    "/run/athena-alpha-m1",
    "/root/athena-alpha-m1-stage",
    "/opt/athena-qualification/build",
    "/var/tmp/athena-d67-ext4",
]
SKIP_CONTENT = {"/usr/local/sbin/athena-alpha-m1-runner", "/etc/sudoers.d/athena-alpha-m1"}
SENSITIVE_NAME = {"athena-alpha-m1-sudoers", "athena-alpha-m1-runner", "gateway_support.py", "athena-alpha-m1-systemd-canary.py", "sudoers"}

def fail(msg):
    print("M28 acquisition: " + msg, file=sys.stderr)
    raise SystemExit(2)

if os.geteuid() not in (0, os.getuid()):
    fail("unexpected effective identity")
test_root = os.environ.get("MONAD_M28_TEST_ROOT")
if platform.system() != "Linux":
    fail("expected Linux")
try:
    if test_root:
        if os.geteuid() == 0:
            fail("synthetic mode is forbidden as root")
        os_release='ID=debian\nVERSION_ID="13"\n'
        machine_id="synthetic-machine-id"; serial="synthetic-virtualbox-serial"; boot_id="synthetic-boot-id"
        vendor="Oracle Corporation"
    else:
        os_release = open("/etc/os-release", encoding="utf-8").read()
        machine_id = open("/etc/machine-id", encoding="ascii").read().strip()
        serial = open("/sys/class/dmi/id/product_serial", encoding="utf-8").read().strip()
        boot_id = open("/proc/sys/kernel/random/boot_id", encoding="ascii").read().strip()
        vendor = open("/sys/class/dmi/id/sys_vendor", encoding="utf-8").read().strip()
except OSError as e:
    fail("cannot establish VM identity: " + str(e))
if 'ID=debian' not in os_release or 'VERSION_ID="13"' not in os_release or not serial or not machine_id or not boot_id:
    fail("host does not satisfy Debian 13 VM identity preflight")
if not any(x in vendor.lower() for x in ("innotek", "oracle")):
    fail("VirtualBox vendor identity not detected")
if not os.path.isabs(OUT) or os.path.normpath(OUT) != OUT or os.path.lexists(OUT):
    fail("output must be a new, normalized absolute path")
for protected in ("/etc", "/usr", "/var", "/run", "/root", "/opt"):
    if OUT == protected or OUT.startswith(protected + "/"):
        fail("evidence output must be outside administrative and source trees")
parent=os.path.dirname(OUT)
try:
    pfd=os.open("/",os.O_RDONLY|os.O_DIRECTORY|os.O_CLOEXEC|os.O_NOFOLLOW)
    try:
        for component in [x for x in parent.split("/") if x]:
            nxt=os.open(component,os.O_RDONLY|os.O_DIRECTORY|os.O_CLOEXEC|os.O_NOFOLLOW,dir_fd=pfd)
            os.close(pfd); pfd=nxt
    finally: os.close(pfd)
except OSError as e:
    fail("output parent must already exist as a no-symlink directory chain: " + str(e))
try:
    os.mkdir(OUT, 0o700)
    os.chmod(OUT, 0o700)
except OSError as e:
    fail("cannot create new evidence directory: " + str(e))

# Test-only remapping is accepted only for non-root synthetic tests. The
# allowlist remains byte-for-byte fixed; each path is mapped beneath a fixture.
if test_root:
    if os.geteuid() == 0 or not os.path.isdir(test_root) or os.path.islink(test_root):
        fail("invalid synthetic test root")
    def map_path(p): return os.path.join(test_root, p.lstrip("/"))
else:
    def map_path(p): return p

def identity(fd):
    st = os.fstat(fd)
    return {"type": stat.filemode(st.st_mode), "uid": st.st_uid, "gid": st.st_gid,
            "mode": format(stat.S_IMODE(st.st_mode), "04o"), "size": st.st_size,
            "mtime_ns": st.st_mtime_ns, "ctime_ns": st.st_ctime_ns,
            "atime_ns": st.st_atime_ns, "inode": st.st_ino, "device": st.st_dev,
            "nlink": st.st_nlink}

def hash_fd(fd):
    h = hashlib.sha256()
    os.lseek(fd, 0, os.SEEK_SET)
    while True:
        b = os.read(fd, 1024*1024)
        if not b: break
        h.update(b)
    return h.hexdigest()

def open_nofollow(path):
    target = map_path(path)
    parts=[p for p in target.split("/") if p]
    fd=os.open("/",os.O_RDONLY|os.O_DIRECTORY|os.O_CLOEXEC|os.O_NOFOLLOW)
    try:
        for i,part in enumerate(parts):
            flags=os.O_RDONLY|os.O_CLOEXEC|os.O_NOFOLLOW|os.O_NONBLOCK
            if i != len(parts)-1: flags |= os.O_DIRECTORY
            nxt=os.open(part,flags,dir_fd=fd)
            os.close(fd); fd=nxt
        return fd
    except BaseException:
        os.close(fd); raise

def walk_fd(path, fd, base_dev, records, errors):
    st = os.fstat(fd)
    meta = identity(fd)
    if st.st_dev != base_dev:
        records.append({"path":path,"status":"filesystem-boundary","metadata":meta}); errors.append(path); return
    rec={"path":path,"status":"present","metadata":meta}
    if stat.S_ISREG(st.st_mode):
        if path not in SKIP_CONTENT and os.path.basename(path) not in SENSITIVE_NAME:
            rec["sha256"]=hash_fd(fd)
        else: rec["sha256_status"]="intentionally-not-read-sensitive-or-executable"
        after=os.fstat(fd)
        if (st.st_dev,st.st_ino,st.st_size,st.st_mtime_ns,st.st_ctime_ns)!=(after.st_dev,after.st_ino,after.st_size,after.st_mtime_ns,after.st_ctime_ns):
            rec["status"]="changed-during-read"; errors.append(path)
        records.append(rec); return
    records.append(rec)
    if not stat.S_ISDIR(st.st_mode): return
    try: names=sorted(os.listdir(fd))
    except OSError as e:
        rec["inventory_error"]=str(e); errors.append(path); return
    for name in names:
        child=path.rstrip("/")+"/"+name
        try: child_fd=os.open(name,os.O_RDONLY|os.O_CLOEXEC|os.O_NOFOLLOW|os.O_NONBLOCK,dir_fd=fd)
        except FileNotFoundError:
            records.append({"path":child,"status":"disappeared-during-traversal"}); errors.append(child); continue
        except OSError as e:
            try: lst=os.stat(name,dir_fd=fd,follow_symlinks=False)
            except OSError: lst=None
            if lst is not None and stat.S_ISLNK(lst.st_mode):
                records.append({"path":child,"status":"symlink-not-followed","metadata":{"type":stat.filemode(lst.st_mode),"uid":lst.st_uid,"gid":lst.st_gid,"mode":format(stat.S_IMODE(lst.st_mode),"04o"),"size":lst.st_size,"mtime_ns":lst.st_mtime_ns,"ctime_ns":lst.st_ctime_ns,"atime_ns":lst.st_atime_ns,"inode":lst.st_ino,"device":lst.st_dev,"nlink":lst.st_nlink}})
            else: records.append({"path":child,"status":"error","error":str(e)})
            errors.append(child); continue
        try: walk_fd(child,child_fd,base_dev,records,errors)
        finally: os.close(child_fd)

def walk(path, base_dev, records, errors):
    try:
        fd = open_nofollow(path)
    except FileNotFoundError:
        records.append({"path":path,"status":"absent"}); return
    except OSError as e:
        records.append({"path":path,"status":"error","error":str(e)}); errors.append(path); return
    try:
        # Each allowlisted root defines its own intended filesystem. Descendants
        # crossing away from that device are recorded but never traversed.
        root_device=os.fstat(fd).st_dev
        walk_fd(path,fd,root_device,records,errors)
    finally: os.close(fd)

records=[]; errors=[]
try:
    for path in EXPECTED: walk(path,None,records,errors)
    doc={"schema":1,"acquisition_utc":datetime.datetime.now(datetime.timezone.utc).isoformat(),
         "identity":{"machine_id":machine_id,"virtualbox_product_serial":serial,"boot_id":boot_id,
                     "os_release":"Debian 13","system_vendor":vendor},
         "allowlist":EXPECTED,"results":records,
         "completion":"COMPLETE" if not errors and all(x["status"] in ("present","absent") for x in records) else "PARTIAL",
         "errors":errors,"provenance":"local acquisition record; not independently authenticated"}
    data=(json.dumps(doc,sort_keys=True,indent=2)+"\n").encode()
    p=os.path.join(OUT,"inventory.json")
    fd=os.open(p,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
    with os.fdopen(fd,"wb") as f: f.write(data); f.flush(); os.fsync(f.fileno())
    digest=hashlib.sha256(data).hexdigest()
    mf=os.open(os.path.join(OUT,"SHA256SUMS"),os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
    with os.fdopen(mf,"w",encoding="ascii") as f: f.write(digest+"  inventory.json\n"); f.flush(); os.fsync(f.fileno())
    os.fsync(os.open(OUT,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW))
except Exception as e:
    print("M28 acquisition: failed after output creation; preserve partial directory: "+str(e),file=sys.stderr)
    raise SystemExit(2)
print(doc["completion"]+": "+OUT)
raise SystemExit(0 if doc["completion"]=="COMPLETE" else 1)
PY
