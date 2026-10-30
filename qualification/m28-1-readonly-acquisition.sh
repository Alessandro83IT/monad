#!/bin/bash
# M28.1 bounded, read-only acquisition. No privileged acquisition is run here.
set -euo pipefail
umask 077
exec /usr/bin/python3 - "$0" "$@" <<'PY'
import datetime, hashlib, json, os, platform, stat, sys

EXPECTED = [
    "/usr/local/sbin/athena-alpha-m1-runner",
    "/usr/local/sbin/gateway_support.py",
    "/etc/sudoers.d/athena-alpha-m1",
    "/var/lib/athena-alpha-m1/admin-install.lock",
    "/var/lib/athena-alpha-m1",
    "/var/lib/athena-alpha-m1/backups",
    "/root/athena-alpha-m1-stage",
    "/run/athena-alpha-m1",
    "/var/log/athena-alpha-m1",
    "/var/lib/athena-alpha-m1/systemd-canary.json",
    "/var/lib/athena-alpha-m1/ext4-fixture",
    "/var/tmp/athena-d67-ext4",
    "/opt/athena-qualification/build",
]
NO_HASH = {"athena-alpha-m1-runner", "gateway_support.py", "athena-alpha-m1-sudoers",
           "athena-alpha-m1-systemd-canary.py"}
DEFAULTS = {"depth": 32, "entries": 10000, "total": 256*1024*1024,
            "file": 64*1024*1024, "output": 8*1024*1024}
HARD = {"depth": 128, "entries": 100000, "total": 2*1024*1024*1024,
        "file": 512*1024*1024, "output": 32*1024*1024}

class Partial(Exception): pass

def fail(msg):
    print("M28.1 acquisition: " + msg, file=sys.stderr)
    raise SystemExit(2)

def main():
    script_path = os.path.realpath(sys.argv[1])
    args = sys.argv[2:]
    test_root = os.environ.get("MONAD_M28_1_TEST_ROOT")
    limits = DEFAULTS.copy()
    if len(args) != 1:
        fail("usage: m28-1-readonly-acquisition.sh NEW_ABSOLUTE_OUTPUT_DIRECTORY")
    if test_root and os.geteuid() == 0:
        fail("synthetic mode is forbidden as root")
    if not test_root and any(k.startswith("MONAD_M28_1_") for k in os.environ):
        fail("environment path remapping/configuration is forbidden in production mode")
    if test_root:
        for key in limits:
            value = os.environ.get("MONAD_M28_1_LIMIT_"+key.upper())
            if value:
                try: limits[key] = int(value)
                except ValueError: fail("invalid synthetic test limit")
                if not 1 <= limits[key] <= HARD[key]: fail("synthetic test limit outside hard bound")
        if not os.path.isabs(test_root) or os.path.islink(test_root): fail("invalid synthetic root")
        rst = os.stat(test_root, follow_symlinks=False)
        if not stat.S_ISDIR(rst.st_mode) or rst.st_uid != os.getuid() or stat.S_IMODE(rst.st_mode) & 0o077:
            fail("synthetic root must be caller-owned and private")
    if platform.system() != "Linux": fail("expected Linux")
    if test_root:
        ident = {"hostname":"synthetic-debian", "machine_id":"synthetic-machine-id",
                 "hardware_serial":"synthetic-virtualbox-serial", "product_uuid":"synthetic-product-uuid",
                 "boot_id":"synthetic-boot-id", "kernel":"synthetic-kernel"}
    else:
        try:
            def read(p):
                fd=os.open(p, os.O_RDONLY|os.O_CLOEXEC|os.O_NOFOLLOW)
                try:
                    s=os.fstat(fd)
                    if not stat.S_ISREG(s.st_mode): raise OSError("not regular")
                    return os.read(fd, 65536).decode("utf-8").strip()
                finally: os.close(fd)
            release=read("/etc/os-release")
            mid=read("/etc/machine-id")
            serial=read("/sys/class/dmi/id/product_serial")
            uuid=read("/sys/class/dmi/id/product_uuid")
            boot=read("/proc/sys/kernel/random/boot_id")
            vendor=read("/sys/class/dmi/id/sys_vendor").lower()
            host=os.uname().nodename
            if host != "debian" or "ID=debian" not in release or 'VERSION_ID="13"' not in release or mid != "9bdbfd8678b5467cb85e4d8aa5e01fea" or serial != "VirtualBox-e475ea91-72e9-4157-9ee6-583e9ae0462f" or not uuid or not boot or not any(x in vendor for x in ("oracle", "innotek")):
                fail("production VM identity mismatch")
            ident={"hostname":host,"machine_id":mid,"hardware_serial":serial,"product_uuid":uuid,
                   "boot_id":boot,"kernel":platform.release()}
        except OSError as e: fail("cannot establish production VM identity: "+str(e))

    out=args[0]
    if not os.path.isabs(out) or os.path.normpath(out) != out or out == "/": fail("output must be new, absolute and normalized")
    for protected in ("/etc","/usr","/var","/run","/root","/opt"):
        if out == protected or out.startswith(protected+"/"): fail("output is inside a protected tree")
    repo=os.path.realpath(os.path.dirname(os.path.dirname(script_path)))
    if out == repo or out.startswith(repo+"/"): fail("output is inside repository")
    parent,name=os.path.split(out)
    if not name or name in (".","..") or not parent: fail("invalid output directory")
    flags=os.O_RDONLY|os.O_DIRECTORY|os.O_CLOEXEC|os.O_NOFOLLOW
    pfd=os.open("/",flags)
    try:
        for comp in [x for x in parent.split("/") if x]:
            nxt=os.open(comp,flags,dir_fd=pfd)
            cs=os.fstat(nxt)
            writable=stat.S_IMODE(cs.st_mode)&0o022
            sticky_tmp=bool(cs.st_mode&stat.S_ISVTX)
            if writable and not sticky_tmp:
                os.close(nxt); fail("unsafe output parent component ownership or permissions")
            os.close(pfd); pfd=nxt
        ps=os.fstat(pfd)
        if ps.st_uid != os.geteuid() or stat.S_IMODE(ps.st_mode)&0o022:
            fail("output parent must be owned by effective user and not group/world writable")
        os.mkdir(name,0o700,dir_fd=pfd)
        dfd=os.open(name,flags,dir_fd=pfd)
        ds=os.fstat(dfd)
        if not stat.S_ISDIR(ds.st_mode) or ds.st_uid != os.geteuid() or stat.S_IMODE(ds.st_mode)!=0o700:
            fail("unsafe created output directory")
    except FileExistsError: fail("output directory already exists")
    except OSError as e: fail("cannot securely create output: "+str(e))
    finally: os.close(pfd)

    records=[]; problems=[]; count=0; hashed=0
    def mapped(path): return os.path.join(test_root,path.lstrip("/")) if test_root else path
    def open_dir_chain(path):
        current=os.open("/",flags)
        try:
            for component in [x for x in path.split("/") if x]:
                nxt=os.open(component,flags,dir_fd=current)
                os.close(current); current=nxt
            return current
        except BaseException:
            os.close(current); raise
    def meta(st):
        return {"type":stat.filemode(st.st_mode),"uid":st.st_uid,"gid":st.st_gid,
                "mode":format(stat.S_IMODE(st.st_mode),"04o"),"size":st.st_size,
                "mtime_ns":st.st_mtime_ns,"ctime_ns":st.st_ctime_ns,"inode":st.st_ino,"device":st.st_dev}
    def mount_id(fd):
        try:
            with open("/proc/self/fdinfo/"+str(fd),encoding="ascii") as info:
                for line in info:
                    if line.startswith("mnt_id:"): return int(line.split()[1])
        except (OSError,ValueError): pass
        return None
    def names_snapshot(fd,limit):
        names=[]; overflow=False
        with os.scandir(fd) as entries:
            for entry in entries:
                if len(names) >= limit:
                    overflow=True; break
                names.append(entry.name)
        return sorted(names),overflow
    def issue(path,limit):
        problems.append(path); records.append({"path":path,"status":"ERROR","limit_exceeded":limit})
    def walk(parentfd,name,path,depth,base_dev=None,base_mount=None,classified=None):
        nonlocal count,hashed
        if count >= limits["entries"]: issue(path,"entries"); return
        count+=1
        try: before=classified if classified is not None else os.stat(name,dir_fd=parentfd,follow_symlinks=False)
        except OSError as e:
            records.append({"path":path,"status":"ERROR","error":str(e)}); problems.append(path); return
        if stat.S_ISLNK(before.st_mode):
            records.append({"path":path,"status":"SYMLINK_NOT_FOLLOWED","metadata":meta(before)}); problems.append(path); return
        if not (stat.S_ISREG(before.st_mode) or stat.S_ISDIR(before.st_mode)):
            records.append({"path":path,"status":"ERROR","metadata":meta(before),"error":"special file not opened"}); problems.append(path); return
        if base_dev is not None and before.st_dev != base_dev:
            records.append({"path":path,"status":"FILESYSTEM_BOUNDARY","metadata":meta(before)}); problems.append(path); return
        fl=os.O_RDONLY|os.O_CLOEXEC|os.O_NOFOLLOW|os.O_NONBLOCK
        if stat.S_ISDIR(before.st_mode): fl|=os.O_DIRECTORY
        try: fd=os.open(name,fl,dir_fd=parentfd)
        except OSError as e:
            records.append({"path":path,"status":"ERROR","error":str(e)}); problems.append(path); return
        try:
            opened=os.fstat(fd)
            if (opened.st_dev,opened.st_ino,stat.S_IFMT(opened.st_mode)) != (before.st_dev,before.st_ino,stat.S_IFMT(before.st_mode)):
                records.append({"path":path,"status":"CHANGED","error":"classified/opened identity mismatch"}); problems.append(path); return
            opened_mount=mount_id(fd)
            if base_mount is not None and opened_mount is not None and opened_mount != base_mount:
                records.append({"path":path,"status":"FILESYSTEM_BOUNDARY","metadata":meta(opened),"mount_id":opened_mount}); problems.append(path); return
            dev=base_dev if base_dev is not None else opened.st_dev
            root_mount=base_mount if base_mount is not None else opened_mount
            rec={"path":path,"status":"PRESENT","metadata_before":meta(opened)}; records.append(rec)
            if stat.S_ISREG(opened.st_mode):
                if opened.st_size > limits["file"]: rec["status"]="ERROR"; rec["limit_exceeded"]="file_size"; problems.append(path); return
                sensitive=os.path.basename(path) in NO_HASH or path in ("/etc/sudoers.d/athena-alpha-m1","/usr/local/sbin/athena-alpha-m1-runner")
                if not sensitive:
                    if hashed+opened.st_size > limits["total"]: rec["status"]="ERROR"; rec["limit_exceeded"]="total_bytes"; problems.append(path); return
                    h=hashlib.sha256(); total=0
                    injected=False
                    while True:
                        b=os.read(fd,min(1024*1024,limits["total"]-hashed-total+1))
                        if not b: break
                        total+=len(b)
                        if hashed+total>limits["total"]: rec["status"]="ERROR"; rec["limit_exceeded"]="total_bytes"; problems.append(path); return
                        h.update(b)
                        if test_root and not injected and os.environ.get("MONAD_M28_1_TEST_MUTATE_FILE") == path:
                            injected=True
                            with open(mapped(path),"ab") as changed: changed.write(b"mutation")
                    hashed+=total; rec["sha256"]=h.hexdigest()
                else: rec["content_status"]="intentionally_not_hashed_sensitive_administrative_executable"
                after=os.fstat(fd)
                if (opened.st_dev,opened.st_ino,opened.st_size,opened.st_mtime_ns,opened.st_ctime_ns)!=(after.st_dev,after.st_ino,after.st_size,after.st_mtime_ns,after.st_ctime_ns):
                    rec["status"]="CHANGED"; problems.append(path)
                rec["metadata_after"]=meta(after); return
            if depth >= limits["depth"]:
                rec["status"]="ERROR"; rec["limit_exceeded"]="depth"; problems.append(path); return
            try: names,overflow=names_snapshot(fd,max(0,limits["entries"]-count))
            except OSError as e: rec["status"]="ERROR"; rec["error"]=str(e); problems.append(path); return
            if overflow:
                rec["status"]="ERROR"; rec["limit_exceeded"]="entries"; problems.append(path); return
            if test_root and os.environ.get("MONAD_M28_1_TEST_MUTATE_DIRECTORY") == path:
                open(os.path.join(mapped(path),"injected-entry"),"wb").close()
            for child in names:
                try: walk(fd,child,path.rstrip("/")+"/"+child,depth+1,dev,root_mount)
                except RecursionError: issue(path,"depth")
            try: names_after,overflow_after=names_snapshot(fd,limits["entries"])
            except OSError as e: rec["status"]="ERROR"; rec["error"]=str(e); problems.append(path); return
            try: after=os.fstat(fd)
            except OSError as e: rec["status"]="ERROR"; rec["error"]=str(e); problems.append(path); return
            if overflow_after:
                rec["status"]="ERROR"; rec["limit_exceeded"]="entries"; problems.append(path)
            if overflow_after or names != names_after or (opened.st_dev,opened.st_ino,opened.st_mtime_ns,opened.st_ctime_ns)!=(after.st_dev,after.st_ino,after.st_mtime_ns,after.st_ctime_ns):
                rec["status"]="CHANGED"; rec["directory_entries_changed"]=(names != names_after); problems.append(path)
            rec["metadata_after"]=meta(after)
        finally: os.close(fd)
    for root in EXPECTED:
        target=mapped(root); par,base=os.path.split(target)
        try:
            rfd=open_dir_chain(par)
            try:
                lst=os.stat(base,dir_fd=rfd,follow_symlinks=False)
                if stat.S_ISLNK(lst.st_mode):
                    records.append({"path":root,"status":"SYMLINK_NOT_FOLLOWED","metadata":meta(lst)}); problems.append(root)
                else: walk(rfd,base,root,0)
            finally: os.close(rfd)
        except FileNotFoundError: records.append({"path":root,"status":"ABSENT"})
        except OSError as e: records.append({"path":root,"status":"ERROR","error":str(e)}); problems.append(root)
    doc={"schema":2,"acquisition_utc":datetime.datetime.now(datetime.timezone.utc).isoformat(),"identity":ident,
         "allowlist":EXPECTED,"limits":limits,"entry_count":count,"bytes_hashed":hashed,"results":records,
         "completion":"PARTIAL" if problems else "COMPLETE","issues":problems,
         "provenance":"local acquisition record; not independently authenticated"}
    def write_exclusive(filename,data):
        fd=os.open(filename,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_CLOEXEC|os.O_NOFOLLOW,0o600,dir_fd=dfd)
        try:
            with os.fdopen(fd,"wb",closefd=False) as f: f.write(data); f.flush(); os.fsync(fd)
        finally: os.close(fd)
    try:
        data=(json.dumps(doc,sort_keys=True,indent=2)+"\n").encode()
        if len(data)>limits["output"]:
            doc={"schema":2,"acquisition_utc":doc["acquisition_utc"],"identity":ident,
                 "allowlist":EXPECTED,"limits":limits,"completion":"PARTIAL",
                 "issues":[{"path":"/","limit_exceeded":"output_size",
                            "affected_paths":EXPECTED,"inventory_records_omitted":len(records)}],
                 "provenance":"local acquisition record; inventory omitted by output-size limit"}
            data=(json.dumps(doc,sort_keys=True,separators=(",",":"))+"\n").encode()
            if len(data)>limits["output"]: fail("output-size limit too small to record partial result")
        write_exclusive("inventory.json",data)
        write_exclusive("SHA256SUMS",(hashlib.sha256(data).hexdigest()+"  inventory.json\n").encode())
        os.fsync(dfd)
    finally: os.close(dfd)
    print(doc["completion"]+": "+out)
    return 0 if doc["completion"]=="COMPLETE" else 1
sys.exit(main())
PY
