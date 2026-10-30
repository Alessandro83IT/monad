#!/bin/bash
# M34.1 targeted, read-only administrative evidence acquisition.
set -euo pipefail
umask 077
exec /usr/bin/python3 -I - "$0" "$@" <<'PY'
import ctypes, datetime, hashlib, json, os, platform, signal, stat, sys

ROOTS = ("usr/local/sbin", "etc/sudoers.d", "root/athena-alpha-m1-stage",
 "var/lib/athena-alpha-m1",
 "var/lib/monad-m32-1-evidence-20261029T194120Z")
REQUIRED = ("/usr/local/sbin/athena-alpha-m1-runner", "/usr/local/sbin/gateway_support.py",
 "/etc/sudoers.d/athena-alpha-m1", "/var/lib/athena-alpha-m1/backups/run-20261021-061921",
 "/var/lib/monad-m32-1-evidence-20261029T194120Z/inventory.json",
 "/var/lib/monad-m32-1-evidence-20261029T194120Z/SHA256SUMS")
CONTENT_NAMES = frozenset(("transaction.json", "backup-manifest.json", "install-started", "install-complete",
 "install-partial", "rollback-started", "rollback-complete", "prepared",
 "inventory.json", "SHA256SUMS", "runner.absent", "support.absent", "sudoers.absent",
 "runner.existed", "support.existed", "sudoers.existed"))
MAX_DEPTH, MAX_ENTRIES, MAX_ENUM, MAX_FILE, MAX_TOTAL, MAX_OUTPUT = 12, 4000, 8000, 4*1024*1024, 32*1024*1024, 8*1024*1024
DIR_FLAGS = os.O_RDONLY|os.O_DIRECTORY|os.O_CLOEXEC|os.O_NOFOLLOW
FILE_FLAGS = os.O_RDONLY|os.O_CLOEXEC|os.O_NOFOLLOW|os.O_NONBLOCK

class Block(Exception): pass

def validate_configuration():
    if len(ROOTS)!=len(set(ROOTS)) or len(REQUIRED)!=len(set(REQUIRED)):
        raise Block("duplicate configured acquisition path")
    if any(not p or p.startswith("/") or ".." in p.split("/") for p in ROOTS):
        raise Block("invalid configured root")
    absolute_roots=tuple("/"+p for p in ROOTS)
    if any(not any(p==r or p.startswith(r+"/") for r in absolute_roots) for p in REQUIRED):
        raise Block("required path outside configured roots")
    if not (0<MAX_DEPTH<=32 and 0<MAX_ENTRIES<=10000 and 0<MAX_ENUM<=20000 and
            0<MAX_FILE<=8*1024*1024 and 0<MAX_TOTAL<=64*1024*1024 and 0<MAX_OUTPUT<=16*1024*1024):
        raise Block("configured resource bound outside hard ceiling")
    if not CONTENT_NAMES or any("/" in n or n in (".","..") for n in CONTENT_NAMES):
        raise Block("invalid content-name allowlist")

def meta(s):
    return {"type":stat.filemode(s.st_mode),"mode":format(stat.S_IMODE(s.st_mode),"04o"),"uid":s.st_uid,"gid":s.st_gid,
            "size":s.st_size,"device":s.st_dev,"inode":s.st_ino,"nlink":s.st_nlink,
            "mtime_ns":s.st_mtime_ns,"ctime_ns":s.st_ctime_ns}

def content_allowed(rel):
    if rel in ("/usr/local/sbin/athena-alpha-m1-runner", "/usr/local/sbin/gateway_support.py",
               "/etc/sudoers.d/athena-alpha-m1", "/root/athena-alpha-m1-stage/gateway_support.py",
               "/var/lib/monad-m32-1-evidence-20261029T194120Z/inventory.json",
               "/var/lib/monad-m32-1-evidence-20261029T194120Z/SHA256SUMS"):
        return True
    backup="/var/lib/athena-alpha-m1/backups/run-20261021-061921/"
    if rel.startswith(backup):
        base=rel[len(backup):]
        return "/" not in base and base in CONTENT_NAMES
    return False

def hash_allowed(rel):
    return content_allowed(rel) or rel.startswith("/var/lib/athena-alpha-m1/backups/run-20261021-061921/")

def descriptor_mount_id(fd):
    """Read Linux STATX_MNT_ID from an already-open descriptor (not a path)."""
    libc=ctypes.CDLL(None, use_errno=True)
    fn=getattr(libc,"statx",None)
    if fn is None: raise Block("statx mount identity unavailable")
    buf=ctypes.create_string_buffer(256)
    fn.argtypes=(ctypes.c_int,ctypes.c_char_p,ctypes.c_int,ctypes.c_uint,ctypes.c_void_p)
    fn.restype=ctypes.c_int
    if fn(fd,b"",0x1000,0x1000,buf)!=0: raise Block("statx mount identity failed: errno "+str(ctypes.get_errno()))
    mask=ctypes.c_uint.from_buffer(buf,0).value
    if not mask & 0x1000: raise Block("statx did not provide mount identity")
    return ctypes.c_ulonglong.from_buffer(buf,144).value

def read_fixed_identity(path):
    root=os.open("/",DIR_FLAGS)
    try:
        parts=path.split("/")[1:]; parent=root
        for component in parts[:-1]:
            nxt=os.open(component,DIR_FLAGS,dir_fd=parent)
            st=os.fstat(nxt)
            if st.st_uid!=0 or stat.S_IMODE(st.st_mode)&0o022:
                os.close(nxt); raise Block("unsafe identity path component: "+component)
            if parent!=root: os.close(parent)
            parent=nxt
        fd=os.open(parts[-1],FILE_FLAGS,dir_fd=parent)
        try:
            st=os.fstat(fd)
            if not stat.S_ISREG(st.st_mode) or st.st_uid!=0 or stat.S_IMODE(st.st_mode)&0o022 or st.st_size>4096: raise Block("invalid identity file: "+path)
            data=os.read(fd,4097)
            if len(data)>4096: raise Block("identity file limit: "+path)
            return data.decode("ascii").strip()
        finally: os.close(fd)
    finally:
        if parent!=root: os.close(parent)
        os.close(root)

def main():
    validate_configuration()
    if len(sys.argv) != 3: raise Block("usage: m34-1-targeted-forensic-acquisition.sh NEW_ABSOLUTE_OUTPUT")
    test_root=os.environ.get("MONAD_M34_TEST_ROOT")
    if test_root and os.geteuid()==0: raise Block("synthetic mode forbidden as root")
    if not test_root and any(k.startswith("MONAD_M34_") for k in os.environ): raise Block("test controls forbidden")
    if platform.system()!="Linux": raise Block("Linux required")
    root_path=test_root or "/"
    if not os.path.isabs(root_path) or os.path.normpath(root_path)!=root_path: raise Block("invalid synthetic root")
    rootfd=os.open(root_path,DIR_FLAGS)
    ident={"mode":"synthetic" if test_root else "production","kernel":platform.release(),"observed_utc":datetime.datetime.now(datetime.timezone.utc).isoformat()}
    issues=[]; results=[]; captured=[]; counts={"entries":0,"enumerated":0,"bytes_read":0}; blocking=False
    try:
        rs=os.fstat(rootfd)
        if not stat.S_ISDIR(rs.st_mode): raise Block("root is not a directory")
        if test_root and os.environ.get("MONAD_M34_TEST_MOUNT_ID_UNAVAILABLE")=="1": raise Block("descriptor mount identity unavailable (synthetic test)")
        descriptor_mount_id(rootfd) # Refuse collection on kernels/libc without descriptor-bound mount IDs.
        if test_root:
            if rs.st_uid!=os.getuid() or stat.S_IMODE(rs.st_mode)&0o077: raise Block("synthetic root must be caller-owned/private")
            ident.update({"hostname":"synthetic","machine_id":"synthetic","boot_id":"synthetic","product_uuid":"synthetic","os":"Debian 13 synthetic fixture"})
        else:
            # Production identity is deliberately pinned to the reviewed Alpha M1 VM.
            hostname=os.uname().nodename; mid=read_fixed_identity("/etc/machine-id"); boot=read_fixed_identity("/proc/sys/kernel/random/boot_id")
            uuid=read_fixed_identity("/sys/devices/virtual/dmi/id/product_uuid"); serial=read_fixed_identity("/sys/devices/virtual/dmi/id/product_serial"); vendor=read_fixed_identity("/sys/devices/virtual/dmi/id/sys_vendor").lower()
            # No VM snapshot inventory is visible from inside the guest; this is not host-side attestation.
            if hostname!="debian" or mid!="9bdbfd8678b5467cb85e4d8aa5e01fea" or uuid!="91ea75e4-e972-5741-9ee6-583e9ae0462f" or serial!="VirtualBox-e475ea91-72e9-4157-9ee6-583e9ae0462f" or not any(v in vendor for v in ("oracle","innotek")):
                raise Block("production guest identity mismatch")
            ident.update({"hostname":hostname,"machine_id":mid,"boot_id":boot,"product_uuid":uuid,"product_serial":serial,"vendor":vendor})
        out=sys.argv[2]
        if not os.path.isabs(out) or os.path.normpath(out)!=out or out=="/": raise Block("output must be absolute, normalized, and new")
        for scope in ("/usr/local/sbin","/etc/sudoers.d","/root/athena-alpha-m1-stage","/var/lib/athena-alpha-m1","/var/lib/athena-alpha-m1/backups/run-20261021-061921","/var/lib/monad-m32-1-evidence-20261029T194120Z"):
            if out==scope or out.startswith(scope+"/"): raise Block("output inside acquisition scope")
        if test_root and (out==test_root or out.startswith(test_root+"/")): raise Block("output must be outside synthetic source")
        repo=os.path.dirname(os.path.dirname(os.path.realpath(sys.argv[1])))
        if out==repo or out.startswith(repo+"/"): raise Block("output inside repository")
        parent,leaf=os.path.split(out)
        def open_parent_chain(path):
            fd=os.open("/",DIR_FLAGS)
            try:
                for component in path.split("/")[1:]:
                    if not component: continue
                    nxt=os.open(component,DIR_FLAGS,dir_fd=fd)
                    st=os.fstat(nxt); mode=stat.S_IMODE(st.st_mode)
                    sticky_tmp=bool(st.st_mode&stat.S_ISVTX) and bool(mode&0o002)
                    if test_root:
                        if not sticky_tmp and (st.st_uid!=os.getuid() or mode&0o077): os.close(nxt); raise Block("unsafe output path component: "+component)
                    elif not sticky_tmp and (st.st_uid!=0 or mode&0o022): os.close(nxt); raise Block("unsafe output path component: "+component)
                    os.close(fd); fd=nxt
                return fd
            except BaseException:
                os.close(fd); raise
        pfd=open_parent_chain(parent)
        ps=os.fstat(pfd)
        if test_root:
            if ps.st_uid!=os.getuid() or stat.S_IMODE(ps.st_mode)&0o077: raise Block("synthetic output parent must be private")
        elif ps.st_uid!=0 or stat.S_IMODE(ps.st_mode)&0o022: raise Block("output parent must be root-owned and not group/world writable")
        os.mkdir(leaf,0o700,dir_fd=pfd); os.fsync(pfd)
        outfd=os.open(leaf,DIR_FLAGS,dir_fd=pfd)
        ost=os.fstat(outfd)
        if ost.st_uid!=os.geteuid() or stat.S_IMODE(ost.st_mode)!=0o700: raise Block("output directory metadata mismatch")
        expected_uid=os.getuid() if test_root else 0

        def open_dir(parentfd,name):
            fd=os.open(name,DIR_FLAGS,dir_fd=parentfd); st=os.fstat(fd)
            if not stat.S_ISDIR(st.st_mode): os.close(fd); raise Block("not directory")
            if st.st_uid!=expected_uid or stat.S_IMODE(st.st_mode)&0o022:
                os.close(fd); raise Block("unsafe directory owner/permissions: "+name)
            return fd,st
        def name_still_matches(parentfd,name,rel,opened,is_dir):
            if test_root and os.environ.get("MONAD_M34_TEST_REPLACE_AFTER_OPEN")==rel:
                try:
                    os.rename(name,name+".after-open",src_dir_fd=parentfd,dst_dir_fd=parentfd)
                    if is_dir: os.mkdir(name,0o700,dir_fd=parentfd)
                    else:
                        nf=os.open(name,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_CLOEXEC|os.O_NOFOLLOW,0o600,dir_fd=parentfd)
                        os.write(nf,b"replacement"); os.close(nf)
                except OSError: pass
            try: named=os.stat(name,dir_fd=parentfd,follow_symlinks=False)
            except OSError: return False
            return not stat.S_ISLNK(named.st_mode) and (named.st_dev,named.st_ino)==(opened.st_dev,opened.st_ino)
        def walk(parentfd,name,rel,depth,mount):
            nonlocal blocking
            if counts["entries"]>=MAX_ENTRIES: issues.append({"path":rel,"status":"LIMIT_ENTRIES"}); return
            counts["entries"]+=1
            try: before=os.stat(name,dir_fd=parentfd,follow_symlinks=False)
            except OSError as e: issues.append({"path":rel,"status":"STAT_ERROR","error":str(e)}); return
            if test_root and os.environ.get("MONAD_M34_TEST_REPLACE_BEFORE_OPEN")==rel:
                try:
                    os.rename(name,name+".saved",src_dir_fd=parentfd,dst_dir_fd=parentfd)
                    if stat.S_ISDIR(before.st_mode): os.mkdir(name,0o700,dir_fd=parentfd)
                    else:
                        nf=os.open(name,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_CLOEXEC|os.O_NOFOLLOW,0o600,dir_fd=parentfd)
                        os.write(nf,b"replacement"); os.close(nf)
                except OSError: pass
            row={"path":rel,"metadata_before":meta(before)}
            if stat.S_ISLNK(before.st_mode): row["status"]="SYMLINK_NOT_FOLLOWED"; results.append(row); issues.append({"path":rel,"status":row["status"]}); return
            if not (stat.S_ISREG(before.st_mode) or stat.S_ISDIR(before.st_mode)):
                row["status"]="SPECIAL_FILE_NOT_OPENED"; results.append(row); issues.append({"path":rel,"status":row["status"]}); return
            if before.st_uid!=expected_uid:
                row["status"]="OWNER_MISMATCH"; results.append(row); issues.append({"path":rel,"status":row["status"]}); return
            if stat.S_ISREG(before.st_mode) and before.st_nlink!=1:
                row["status"]="HARDLINK_ANOMALY"; results.append(row); issues.append({"path":rel,"status":row["status"]}); return
            if stat.S_IMODE(before.st_mode)&0o022:
                row["status"]="UNSAFE_PERMISSIONS"; results.append(row); issues.append({"path":rel,"status":row["status"]}); return
            needed=0o500 if stat.S_ISDIR(before.st_mode) else 0o400 if hash_allowed(rel) else 0
            if stat.S_IMODE(before.st_mode)&needed!=needed:
                row["status"]="PERMISSION_ERROR"; results.append(row); issues.append({"path":rel,"status":row["status"]}); return
            try: fd=os.open(name,DIR_FLAGS if stat.S_ISDIR(before.st_mode) else FILE_FLAGS,dir_fd=parentfd)
            except OSError as e: row["status"]="OPEN_ERROR"; row["error"]=str(e); results.append(row); issues.append({"path":rel,"status":row["status"]}); return
            try:
                opened=os.fstat(fd)
                if (opened.st_dev,opened.st_ino,stat.S_IFMT(opened.st_mode))!=(before.st_dev,before.st_ino,stat.S_IFMT(before.st_mode)):
                    row["status"]="IDENTITY_CHANGED"; results.append(row); issues.append({"path":rel,"status":row["status"]}); return
                try:
                    current_mount=descriptor_mount_id(fd)
                    if test_root and os.environ.get("MONAD_M34_TEST_MOUNT_ID_ERROR")==rel: raise Block("injected mount identity error")
                    if test_root and os.environ.get("MONAD_M34_TEST_BIND_MOUNT")==rel: current_mount+=1
                except Block as e:
                    row["status"]="MOUNT_ID_UNAVAILABLE"; row["error"]=str(e); results.append(row); issues.append({"path":rel,"status":row["status"]}); blocking=True; return
                if mount is not None and current_mount!=mount:
                    row["status"]="MOUNT_BOUNDARY"; results.append(row); issues.append({"path":rel,"status":row["status"]}); return
                if stat.S_ISDIR(opened.st_mode):
                    if mount is None: mount=current_mount
                    if rel=="/var/lib/athena-alpha-m1/ext4-fixture":
                        row["status"]="PRESENT_POLICY_PRUNED"; row["metadata_after"]=meta(opened); results.append(row)
                        if not name_still_matches(parentfd,name,rel,opened,True):
                            row["status"]="PATH_REPLACED_DURING_ACQUISITION"; issues.append({"path":rel,"status":row["status"]})
                        return
                    if depth>=MAX_DEPTH: row["status"]="LIMIT_DEPTH"; results.append(row); issues.append({"path":rel,"status":row["status"]}); return
                    try:
                        names=[]
                        with os.scandir(fd) as it:
                            for ent in it:
                                counts["enumerated"]+=1
                                if counts["enumerated"]>MAX_ENUM: raise Block("enumeration limit")
                                names.append(ent.name)
                        names.sort()
                    except (OSError,Block) as e: row["status"]="ENUMERATION_ERROR"; row["error"]=str(e); results.append(row); issues.append({"path":rel,"status":row["status"]}); return
                    row["status"]="PRESENT"; results.append(row)
                    for child in names:
                        if interrupt_requested: break
                        walk(fd,child,rel+"/"+child,depth+1,mount)
                    if test_root and os.environ.get("MONAD_M34_TEST_MUTATE_DIRECTORY")==rel:
                        try:
                            nf=os.open("injected",os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_CLOEXEC|os.O_NOFOLLOW,0o600,dir_fd=fd); os.close(nf)
                        except OSError: pass
                    after=os.fstat(fd)
                    if (opened.st_ino,opened.st_mtime_ns,opened.st_ctime_ns)!=(after.st_ino,after.st_mtime_ns,after.st_ctime_ns): row["status"]="CHANGED_DURING_ACQUISITION"; issues.append({"path":rel,"status":row["status"]})
                    if not name_still_matches(parentfd,name,rel,opened,True): row["status"]="PATH_REPLACED_DURING_ACQUISITION"; issues.append({"path":rel,"status":row["status"]})
                    row["metadata_after"]=meta(after)
                else:
                    row["status"]="PRESENT"; row["content_status"]="metadata_only"
                    if hash_allowed(rel):
                        if before.st_size>MAX_FILE or counts["bytes_read"]+before.st_size>MAX_TOTAL:
                            row["status"]="CONTENT_LIMIT"; issues.append({"path":rel,"status":row["status"]})
                        else:
                            if test_root and os.environ.get("MONAD_M34_TEST_MUTATE_FILE")==rel:
                                try:
                                    mf=os.open(name,os.O_WRONLY|os.O_APPEND|os.O_CLOEXEC|os.O_NOFOLLOW,dir_fd=parentfd)
                                    os.write(mf,b"mutation"); os.close(mf)
                                except OSError: pass
                            chunks=[]; h=hashlib.sha256(); n=0; over_limit=False
                            while True:
                                b=os.read(fd,min(65536,MAX_FILE-n+1))
                                if not b: break
                                n+=len(b)
                                if n>MAX_FILE or counts["bytes_read"]+n>MAX_TOTAL: over_limit=True; break
                                chunks.append(b); h.update(b)
                            data=b"".join(chunks); after=os.fstat(fd)
                            if over_limit:
                                row["status"]="CONTENT_LIMIT"; issues.append({"path":rel,"status":row["status"]})
                                results.append(row); return
                            if len(data)!=before.st_size or (opened.st_ino,opened.st_size,opened.st_mtime_ns,opened.st_ctime_ns)!=(after.st_ino,after.st_size,after.st_mtime_ns,after.st_ctime_ns):
                                row["status"]="CHANGED_DURING_ACQUISITION"; issues.append({"path":rel,"status":row["status"]})
                            if not name_still_matches(parentfd,name,rel,opened,False): row["status"]="PATH_REPLACED_DURING_ACQUISITION"; issues.append({"path":rel,"status":row["status"]})
                            else:
                                counts["bytes_read"]+=n; row["sha256"]=h.hexdigest(); row["metadata_after"]=meta(after)
                                if content_allowed(rel):
                                    row["content_status"]="captured_explicit_name_allowlist"; row["evidence_file"]="captured/"+str(len(captured))+".bin"
                                    captured.append((rel,data,h.hexdigest()))
                                else: row["content_status"]="sha256_only_backup_payload"
                    results.append(row)
            except Block: raise
            except OSError as e:
                issues.append({"path":rel,"status":"READ_ERROR","error":str(e)})
            finally: os.close(fd)
        interrupted=False; interrupt_requested=False
        def request_interrupt(signum,frame):
            nonlocal interrupt_requested
            interrupt_requested=True
        signal.signal(signal.SIGINT,request_interrupt)
        for scope_index,scope in enumerate(ROOTS):
            if (test_root and scope_index>0 and os.environ.get("MONAD_M34_TEST_INTERRUPT_AFTER_FIRST_SCOPE")=="1") or interrupt_requested:
                issues.append({"status":"CONTROLLED_INTERRUPTION","completed_scopes":scope_index}); interrupted=True; break
            try:
                cur=os.dup(rootfd)
                try:
                    parts=scope.split("/")
                    for component in parts[:-1]:
                        nxt,_=open_dir(cur,component); os.close(cur); cur=nxt
                    try: walk(cur,parts[-1],"/"+scope,0,None)
                    except FileNotFoundError: results.append({"path":"/"+scope,"status":"ABSENT"})
                finally: os.close(cur)
            except OSError as e: issues.append({"path":"/"+scope,"status":"PATH_ACCESS_ERROR","error":str(e)})
        if interrupt_requested and not interrupted:
            issues.append({"status":"CONTROLLED_INTERRUPTION","completed_scopes":len(ROOTS)}); interrupted=True
        # Deduplicate overlapping scopes by path; collisions are a configuration error.
        paths=[r["path"] for r in results]
        if len(paths)!=len(set(paths)): issues.append({"status":"DUPLICATE_INVENTORY_PATH"})
        source_files={path:data for path,data,_ in captured}
        m32_inv="/var/lib/monad-m32-1-evidence-20261029T194120Z/inventory.json"
        m32_sums="/var/lib/monad-m32-1-evidence-20261029T194120Z/SHA256SUMS"
        try:
            if m32_sums not in source_files or m32_inv not in source_files:
                raise KeyError("M32.1 inventory/checksum bytes unavailable")
            lines=source_files[m32_sums].decode("ascii").splitlines()
            fields=lines[0].split() if len(lines)==1 else []
            if len(fields)!=2 or fields[1]!="inventory.json" or len(fields[0])!=64 or fields[0]!=hashlib.sha256(source_files[m32_inv]).hexdigest():
                raise ValueError("M32.1 SHA256SUMS does not match its inventory")
        except KeyError as e:
            issues.append({"path":m32_sums,"status":"SOURCE_CHECKSUM_UNAVAILABLE","error":str(e)})
        except (UnicodeError,ValueError,IndexError) as e:
            issues.append({"path":m32_sums,"status":"SOURCE_CHECKSUM_INVALID","error":str(e)})
        for required in REQUIRED:
            found=next((r for r in results if r["path"]==required),None)
            expected="directory" if required.endswith("/run-20261021-061921") else "file"
            if found is None:
                inaccessible=any(i.get("path") and required.startswith(i["path"].rstrip("/")+"/") and i.get("status") in ("PATH_ACCESS_ERROR","OPEN_ERROR","ENUMERATION_ERROR","FILESYSTEM_BOUNDARY","STAT_ERROR","OWNER_MISMATCH","UNSAFE_PERMISSIONS") for i in issues)
                if not inaccessible: results.append({"path":required,"status":"REQUIRED_ABSENT","operation":"bounded inventory lookup"})
                issues.append({"path":required,"status":"REQUIRED_EVIDENCE_UNAVAILABLE"})
            elif found.get("status")!="PRESENT": issues.append({"path":required,"status":"REQUIRED_EVIDENCE_UNAVAILABLE"})
            else:
                actual=found.get("metadata_before",{}).get("type","")
                matches=actual.startswith("d") if expected=="directory" else actual.startswith("-")
                if not matches:
                    found["status"]="TYPE_MISMATCH"; found["expected_type"]=expected
                    issues.append({"path":required,"status":"REQUIRED_TYPE_MISMATCH","expected":expected,"observed":actual})
                elif expected=="file" and (not found.get("sha256") or found.get("content_status")!="captured_explicit_name_allowlist"):
                    found["status"]="REQUIRED_CONTENT_UNAVAILABLE"
                    issues.append({"path":required,"status":"REQUIRED_CONTENT_UNAVAILABLE"})
        backup_prefix="/var/lib/athena-alpha-m1/backups/run-20261021-061921/"
        for row in results:
            path=row.get("path","")
            if path.startswith(backup_prefix) and "/" not in path[len(backup_prefix):]:
                name=path[len(backup_prefix):]
                if name.endswith((".absent",".existed")) and name not in CONTENT_NAMES:
                    row["content_status"]="sha256_only_unapproved_marker"
                    issues.append({"path":path,"status":"UNEXPECTED_BACKUP_MARKER"})
        doc={"schema":1,"collector":"M34.1","acquisition_started_utc":ident.pop("observed_utc"),"acquisition_finished_utc":datetime.datetime.now(datetime.timezone.utc).isoformat(),
             "identity":ident,"allowlist":["/"+p for p in ROOTS],"content_name_allowlist":sorted(CONTENT_NAMES),"limits":{"depth":MAX_DEPTH,"entries":MAX_ENTRIES,"enumeration":MAX_ENUM,"per_file_bytes":MAX_FILE,"total_bytes":MAX_TOTAL,"inventory_bytes":MAX_OUTPUT},
             "counts":counts,"results":results,"issues":issues,"completion":"BLOCKED" if interrupted or blocking else "COMPLETE" if not issues and len(results)>=len(ROOTS) else "PARTIAL","provenance":"unsigned local record; live non-atomic acquisition; no host snapshot attestation"}
        inv=(json.dumps(doc,sort_keys=True,indent=2)+"\n").encode()
        if len(inv)>MAX_OUTPUT:
            doc["results"]=[]; doc["completion"]="PARTIAL" if doc["completion"]!="BLOCKED" else "BLOCKED"; doc["issues"].append({"status":"INVENTORY_LIMIT","omitted_records":len(results)}); captured.clear(); inv=(json.dumps(doc,sort_keys=True,separators=(",",":"))+"\n").encode()
        if len(inv)>MAX_OUTPUT: raise Block("minimal inventory exceeds output limit")
        files=[("inventory.json",inv)]+[("captured/"+str(i)+".bin",data) for i,(_,data,_) in enumerate(captured)]
        sums=[]
        for name,data in files:
            if name.startswith("captured/"):
                try: os.mkdir("captured",0o700,dir_fd=outfd)
                except FileExistsError: pass
                sub=os.open("captured",DIR_FLAGS,dir_fd=outfd)
                try: fd=os.open(name.split("/")[1],os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_CLOEXEC|os.O_NOFOLLOW,0o600,dir_fd=sub)
                finally: os.close(sub)
            else: fd=os.open(name,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_CLOEXEC|os.O_NOFOLLOW,0o600,dir_fd=outfd)
            try:
                view=memoryview(data); off=0
                while off<len(view):
                    n=os.write(fd,view[off:])
                    if n<=0: raise OSError("short output write")
                    off+=n
                os.fsync(fd)
            finally: os.close(fd)
            sums.append(hashlib.sha256(data).hexdigest()+"  "+name+"\n")
        if captured:
            sub=os.open("captured",DIR_FLAGS,dir_fd=outfd)
            try: os.fsync(sub)
            finally: os.close(sub)
        sf=os.open("SHA256SUMS",os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_CLOEXEC|os.O_NOFOLLOW,0o600,dir_fd=outfd)
        try:
            checksum_data="".join(sums).encode(); off=0
            while off<len(checksum_data):
                n=os.write(sf,checksum_data[off:])
                if n<=0: raise OSError("checksum write made no progress")
                off+=n
            os.fsync(sf)
        finally: os.close(sf)
        os.fsync(outfd); os.fsync(pfd)
        named=os.stat(leaf,dir_fd=pfd,follow_symlinks=False)
        anchored=os.fstat(outfd)
        if (named.st_dev,named.st_ino)!=(anchored.st_dev,anchored.st_ino): raise Block("output destination changed during acquisition; preserve any partial output")
        check_parent=open_parent_chain(parent)
        try:
            current_parent=os.fstat(check_parent)
            if (current_parent.st_dev,current_parent.st_ino)!=(ps.st_dev,ps.st_ino): raise Block("output parent changed during acquisition; preserve any partial output")
        finally: os.close(check_parent)
        os.close(outfd); os.close(pfd); os.close(rootfd)
        print(doc["completion"]+": "+out)
        return 0 if doc["completion"]=="COMPLETE" else 2 if doc["completion"]=="BLOCKED" else 1
    except BaseException:
        try: os.close(rootfd)
        except OSError: pass
        raise

try: sys.exit(main())
except Exception as exc:
    print("M34.1 BLOCKED: "+str(exc),file=sys.stderr); sys.exit(2)
PY
