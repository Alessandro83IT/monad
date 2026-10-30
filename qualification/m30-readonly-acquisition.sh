#!/bin/bash
# M30 read-only evidence collector. Explicit operator invocation only.
set -euo pipefail
umask 077
exec /usr/bin/python3 -I - "$0" "$@" <<'PY'
import datetime, hashlib, json, os, platform, stat, sys

EXPECTED = ["/usr/local/sbin/athena-alpha-m1-runner", "/usr/local/sbin/gateway_support.py",
 "/etc/sudoers.d/athena-alpha-m1", "/var/lib/athena-alpha-m1/admin-install.lock",
 "/var/lib/athena-alpha-m1", "/var/lib/athena-alpha-m1/backups", "/root/athena-alpha-m1-stage",
 "/run/athena-alpha-m1", "/var/log/athena-alpha-m1", "/var/lib/athena-alpha-m1/systemd-canary.json",
 "/var/lib/athena-alpha-m1/ext4-fixture", "/var/tmp/athena-d67-ext4", "/opt/athena-qualification/build"]
# The fixed scope contains installer outputs, installer preconditions, runtime state,
# and historical targets. See the M30 report for per-path semantics; missing
# historical targets are never treated as proof of historical absence.
OPTIONAL = {"/run/athena-alpha-m1"}
# The installer creates this exact lock file empty (exclusive mode 0600); that
# source-defined invariant makes its contents safe to hash. Everything else is metadata-only.
HASH_ALLOWLIST = {"/var/lib/athena-alpha-m1/admin-install.lock"}
DEFAULT = {"depth":32,"entries":10000,"total":256*1024*1024,"file":64*1024*1024,"output":8*1024*1024}
HARD = {"depth":128,"entries":100000,"total":2*1024*1024*1024,"file":512*1024*1024,"output":32*1024*1024}
FLAGS=os.O_RDONLY|os.O_DIRECTORY|os.O_CLOEXEC|os.O_NOFOLLOW

def stop(msg):
 print("M30 acquisition: "+msg,file=sys.stderr); raise SystemExit(2)
def meta(s):
 return {"type":stat.filemode(s.st_mode),"uid":s.st_uid,"gid":s.st_gid,"mode":format(stat.S_IMODE(s.st_mode),"04o"),"size":s.st_size,"mtime_ns":s.st_mtime_ns,"ctime_ns":s.st_ctime_ns,"inode":s.st_ino,"device":s.st_dev}
def main():
 script=sys.argv[1]; args=sys.argv[2:]
 if len(args)!=1: stop("usage: m30-readonly-acquisition.sh NEW_ABSOLUTE_OUTPUT_DIRECTORY")
 test=os.environ.get("MONAD_M30_TEST_ROOT"); lim=DEFAULT.copy()
 if test and os.geteuid()==0: stop("synthetic mode is forbidden as root")
 if not test and any(k.startswith("MONAD_M30_") for k in os.environ): stop("test configuration forbidden in production mode")
 if test:
  for k in lim:
   v=os.environ.get("MONAD_M30_LIMIT_"+k.upper())
   if v:
    try: lim[k]=int(v)
    except ValueError: stop("invalid test limit")
    if not 1<=lim[k]<=HARD[k]: stop("test limit outside hard bound")
  if not os.path.isabs(test) or os.path.normpath(test)!=test: stop("invalid synthetic root")
 if platform.system()!="Linux": stop("expected Linux")
 test_root_fd=None
 if test:
  test_root_fd=os.open("/",FLAGS)
  try:
   for component in test.split("/")[1:]:
    if not component: continue
    nxt=os.open(component,FLAGS,dir_fd=test_root_fd); os.close(test_root_fd); test_root_fd=nxt
   ts=os.fstat(test_root_fd)
   if not stat.S_ISDIR(ts.st_mode) or ts.st_uid!=os.getuid() or stat.S_IMODE(ts.st_mode)&0o077: stop("synthetic root must be caller-owned and private")
  except BaseException:
   os.close(test_root_fd); raise
  if os.environ.get("MONAD_M30_TEST_REPLACE_ROOT")=="1":
   moved=test+".anchored"; os.rename(test,moved)
   decoy=test+".decoy"; os.mkdir(decoy,0o700); os.symlink(decoy,test)
 hash_allowlist=set(HASH_ALLOWLIST)
 if test and os.environ.get("MONAD_M30_TEST_HASH_PATH"):
  synthetic_hash=os.environ["MONAD_M30_TEST_HASH_PATH"]
  if not any(synthetic_hash==p or synthetic_hash.startswith(p+"/") for p in EXPECTED): stop("synthetic hash path is outside acquisition roots")
  hash_allowlist.add(synthetic_hash)
 if test: ident={"hostname":"synthetic-debian","machine_id":"synthetic-machine-id","hardware_serial":"synthetic-virtualbox-serial","product_uuid":"synthetic-product-uuid","boot_id":"synthetic-boot-id","kernel":"synthetic-kernel"}
 else:
  def read_identity(p):
   fd=os.open(p,os.O_RDONLY|os.O_CLOEXEC|os.O_NOFOLLOW)
   try:
    if not stat.S_ISREG(os.fstat(fd).st_mode): raise OSError("not regular")
    return os.read(fd,65536).decode().strip()
   finally: os.close(fd)
  try:
   release=read_identity("/etc/os-release"); mid=read_identity("/etc/machine-id"); serial=read_identity("/sys/class/dmi/id/product_serial"); uuid=read_identity("/sys/class/dmi/id/product_uuid"); boot=read_identity("/proc/sys/kernel/random/boot_id"); vendor=read_identity("/sys/class/dmi/id/sys_vendor").lower(); host=os.uname().nodename
   if host!="debian" or "ID=debian" not in release or 'VERSION_ID="13"' not in release or mid!="9bdbfd8678b5467cb85e4d8aa5e01fea" or serial!="VirtualBox-e475ea91-72e9-4157-9ee6-583e9ae0462f" or uuid!="91ea75e4-e972-5741-9ee6-583e9ae0462f" or not boot or not any(x in vendor for x in ("oracle","innotek")): stop("production VM identity mismatch")
   ident={"hostname":host,"machine_id":mid,"hardware_serial":serial,"product_uuid":uuid,"boot_id":boot,"kernel":platform.release()}
  except OSError as e: stop("cannot establish production VM identity: "+str(e))
 out=args[0]
 if not os.path.isabs(out) or os.path.normpath(out)!=out or out=="/": stop("output must be new, absolute and normalized")
 if any(out==source or out.startswith(source+"/") for source in EXPECTED): stop("output cannot be inside an acquisition root")
 repo=os.path.realpath(os.path.dirname(os.path.dirname(script)))
 if out==repo or out.startswith(repo+"/"): stop("output cannot be inside repository")
 parent,leaf=os.path.split(out)
 if not leaf or leaf in (".",".."): stop("invalid output directory")
 pfd=os.open("/",FLAGS)
 dfd=None
 try:
  for c in parent.split("/")[1:]:
   if not c: stop("non-normalized output parent")
   n=os.open(c,FLAGS,dir_fd=pfd); s=os.fstat(n)
   sticky_tmp=bool(s.st_mode&stat.S_ISVTX) and stat.S_IMODE(s.st_mode)&0o002
   synthetic_private=bool(test and s.st_uid==os.getuid() and not (stat.S_IMODE(s.st_mode)&0o077))
   if (s.st_uid!=0 or stat.S_IMODE(s.st_mode)&0o022) and not (test and (sticky_tmp or synthetic_private)): os.close(n); stop("every output parent must be root-owned and not group/world writable")
   os.close(pfd); pfd=n
  ps=os.fstat(pfd)
  if test:
   if ps.st_uid!=os.getuid() or stat.S_IMODE(ps.st_mode)&0o077: stop("synthetic output parent must be caller-owned and private")
  elif ps.st_uid!=0 or stat.S_IMODE(ps.st_mode)&0o022: stop("output parent must be root-owned and private")
  os.mkdir(leaf,0o700,dir_fd=pfd)
  dfd=os.open(leaf,FLAGS,dir_fd=pfd)
  ds=os.fstat(dfd)
  named=os.stat(leaf,dir_fd=pfd,follow_symlinks=False)
  if not stat.S_ISDIR(ds.st_mode) or ds.st_uid!=os.geteuid() or stat.S_IMODE(ds.st_mode)!=0o700 or (ds.st_dev,ds.st_ino)!=(named.st_dev,named.st_ino): stop("created output directory identity/mode mismatch")
  if test and os.environ.get("MONAD_M30_TEST_REPLACE_OUTPUT")=="1":
   os.rename(leaf,leaf+".anchored",src_dir_fd=pfd,dst_dir_fd=pfd)
   os.mkdir(leaf,0o700,dir_fd=pfd)
  output_parent_fd=os.dup(pfd)
 except FileExistsError: stop("output destination already exists")
 except OSError as e: stop("unsafe output parent or creation failure: "+str(e))
 finally:
  os.close(pfd)
 root=test or ""; records=[]; issues=[]; count=0; hashed=0; enum_scanned=0
 def mapped(p): return p
 def open_chain(path):
  f=os.dup(test_root_fd) if test else os.open("/",FLAGS)
  try:
   for c in path.split("/")[1:]:
    n=os.open(c,FLAGS,dir_fd=f); os.close(f); f=n
   return f
  except BaseException: os.close(f); raise
 def mutate_path(path,kind,data=b"x"):
  parent,name=os.path.split(path); fd=open_chain(parent)
  try:
   if kind=="unlink": os.unlink(name,dir_fd=fd)
   elif kind=="write":
    f=os.open(name,os.O_WRONLY|os.O_CREAT|os.O_APPEND|os.O_CLOEXEC|os.O_NOFOLLOW,0o600,dir_fd=fd)
    try: os.write(f,data)
    finally: os.close(f)
   elif kind=="create":
    f=os.open(name,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_CLOEXEC|os.O_NOFOLLOW,0o600,dir_fd=fd)
    try: os.write(f,data)
    finally: os.close(f)
  finally: os.close(fd)
 def add_issue(path,status,error=None):
  r={"path":path,"status":status}
  if error is not None:r["error"]=str(error)
  records.append(r); issues.append(path)
 def mount_id(fd):
  if test and os.environ.get("MONAD_M30_TEST_MOUNT_UNAVAILABLE")=="1": return None
  try:
   with open("/proc/self/fdinfo/"+str(fd),"r",encoding="ascii") as info:
    for line in info:
     if line.startswith("mnt_id:"): return int(line.split(":",1)[1].strip())
  except OSError: return None
  return None
 def injected_boundary(path,kind):
  if not test: return False
  return os.environ.get("MONAD_M30_TEST_"+kind)==path
 def snapshot(fd,cap):
  nonlocal enum_scanned
  names=[]; overflow=False; cap=min(cap,max(0,lim["entries"]-enum_scanned))
  with os.scandir(fd) as it:
   for entry in it:
    if len(names)>=cap: overflow=True; break
    names.append(entry.name); enum_scanned+=1
  names.sort(); return names,overflow
 def walk(pfd,name,path,depth,expected=None,boundary=None):
  nonlocal count,hashed
  if count>=lim["entries"]: add_issue(path,"LIMIT_ENTRIES"); return
  count+=1
  if test and os.environ.get("MONAD_M30_TEST_DISAPPEAR")==path:
   try: mutate_path(path,"unlink")
   except OSError: pass
  try: before=expected if expected is not None else os.stat(name,dir_fd=pfd,follow_symlinks=False)
  except FileNotFoundError as e: add_issue(path,"DISAPPEARED_DURING_TRAVERSAL",e); return
  except OSError as e: add_issue(path,"ACCESS_ERROR",e); return
  if stat.S_ISLNK(before.st_mode): records.append({"path":path,"status":"SYMLINK_NOT_FOLLOWED","metadata":meta(before)}); issues.append(path); return
  if not (stat.S_ISREG(before.st_mode) or stat.S_ISDIR(before.st_mode)):
   records.append({"path":path,"status":"SPECIAL_FILE_NOT_OPENED","metadata":meta(before)}); issues.append(path); return
  fl=os.O_RDONLY|os.O_CLOEXEC|os.O_NOFOLLOW|os.O_NONBLOCK
  if stat.S_ISDIR(before.st_mode): fl|=os.O_DIRECTORY
  try: fd=os.open(name,fl,dir_fd=pfd)
  except FileNotFoundError as e: add_issue(path,"DISAPPEARED_DURING_TRAVERSAL",e); return
  except OSError as e: add_issue(path,"OPEN_ERROR",e); return
  try:
   st=os.fstat(fd)
   if test and os.environ.get("MONAD_M30_TEST_IDENTITY_MISMATCH")==path: add_issue(path,"IDENTITY_MISMATCH","injected synthetic identity mismatch"); return
   if (st.st_dev,st.st_ino,stat.S_IFMT(st.st_mode))!=(before.st_dev,before.st_ino,stat.S_IFMT(before.st_mode)): add_issue(path,"IDENTITY_MISMATCH","classified/opened identity differs"); return
   here_mount=mount_id(fd)
   if boundary is None:
    boundary=(st.st_dev,here_mount)
    if here_mount is None:
     add_issue(path,"MOUNT_ID_UNAVAILABLE","cannot establish root mount identity; descent is conservatively stopped")
     return
   if st.st_dev!=boundary[0] or here_mount is None or here_mount!=boundary[1]:
    add_issue(path,"FILESYSTEM_BOUNDARY",{"root_device":boundary[0],"entry_device":st.st_dev,"root_mount_id":boundary[1],"entry_mount_id":here_mount})
    return
   if injected_boundary(path,"DEVICE_BOUNDARY") or injected_boundary(path,"MOUNT_BOUNDARY"):
    add_issue(path,"FILESYSTEM_BOUNDARY","injected synthetic device/mount boundary")
    return
   rec={"path":path,"status":"PRESENT","metadata_before":meta(st)}; records.append(rec)
   if stat.S_ISREG(st.st_mode):
    if path in hash_allowlist:
     if test and os.environ.get("MONAD_M30_TEST_HASH_ERROR")==path: rec["status"]="HASH_READ_ERROR"; rec["error"]="injected synthetic read error"; issues.append(path); return
     if st.st_size!=0: rec["status"]="HASH_POLICY_MISMATCH"; rec["error"]="source-authorized lock file is not empty"; issues.append(path); return
     if st.st_size>lim["file"] or hashed+st.st_size>lim["total"]: rec["status"]="LIMIT_HASH_BYTES"; issues.append(path); return
     h=hashlib.sha256(); total=0
     try:
      while True:
       b=os.read(fd,min(65536,lim["total"]-hashed-total+1))
       if not b: break
       total+=len(b)
       if total>lim["file"] or hashed+total>lim["total"]: rec["status"]="LIMIT_HASH_BYTES"; issues.append(path); return
       h.update(b)
     except OSError as e: rec["status"]="HASH_READ_ERROR"; rec["error"]=str(e); issues.append(path); return
     hashed+=total; rec["sha256"]=h.hexdigest(); rec["content_status"]="hashed_by_exact_allowlist"
     if test and os.environ.get("MONAD_M30_TEST_MUTATE_FILE")==path:
      mutate_path(path,"write",b"mutation")
    else: rec["content_status"]="metadata_only_not_read"
    after=os.fstat(fd)
    if (st.st_dev,st.st_ino,st.st_size,st.st_mtime_ns,st.st_ctime_ns)!=(after.st_dev,after.st_ino,after.st_size,after.st_mtime_ns,after.st_ctime_ns): rec["status"]="CHANGED_DURING_ACQUISITION"; issues.append(path)
    rec["metadata_after"]=meta(after); return
   if depth>=lim["depth"]: rec["status"]="LIMIT_DEPTH"; issues.append(path); return
   try:
    names,overflow=snapshot(fd,max(0,lim["entries"]-count))
    if overflow:
     rec["status"]="LIMIT_ENTRIES"; rec["verification_status"]="INCOMPLETE_ENTRY_BUDGET"; issues.append(path)
   except OSError as e: rec["status"]="TRAVERSAL_ERROR"; rec["error"]=str(e); issues.append(path); return
   for child in names: walk(fd,child,path.rstrip("/")+"/"+child,depth+1,boundary=boundary)
   if test and os.environ.get("MONAD_M30_TEST_MUTATE_DIRECTORY")==path:
    try: mutate_path(path.rstrip("/")+"/injected-entry","create")
    except FileExistsError: pass
   try:
    after=os.fstat(fd)
    names_after,overflow_after=snapshot(fd,max(0,lim["entries"]-count)+1)
   except OSError as e: rec["status"]="TRAVERSAL_ERROR"; rec["error"]=str(e); issues.append(path); return
   rec["metadata_after"]=meta(after)
   if overflow_after or names!=names_after or (st.st_dev,st.st_ino,st.st_mtime_ns,st.st_ctime_ns)!=(after.st_dev,after.st_ino,after.st_mtime_ns,after.st_ctime_ns): rec["status"]="DIRECTORY_CHANGED_DURING_ACQUISITION"; issues.append(path)
  finally: os.close(fd)
 for path in EXPECTED:
  target=mapped(path); parent,name=os.path.split(target)
  try:
   fd=open_chain(parent)
   try:
    try: s=os.stat(name,dir_fd=fd,follow_symlinks=False)
    except FileNotFoundError as e:
     if path in OPTIONAL: records.append({"path":path,"status":"OPTIONAL_ABSENT","operation":"stat"})
     else:
      records.append({"path":path,"status":"REQUIRED_ABSENT","operation":"stat","error":str(e),"historical_absence_inferred":False})
      issues.append(path)
     continue
    if stat.S_ISLNK(s.st_mode): records.append({"path":path,"status":"SYMLINK_NOT_FOLLOWED","metadata":meta(s)}); issues.append(path)
    else: walk(fd,name,path,0,s)
   finally: os.close(fd)
  except FileNotFoundError as e: add_issue(path,"MISSING_INTERMEDIATE_DIRECTORY",e)
  except OSError as e: add_issue(path,"PATH_ACCESS_ERROR",e)
 stamp=datetime.datetime.now(datetime.timezone.utc).isoformat()
 doc={"schema":4,"acquisition_utc":stamp,"identity":ident,"administrative_allowlist":EXPECTED,"hash_allowlist":sorted(hash_allowlist),"limits":lim,"entry_count":count,"bytes_hashed":hashed,"results":records,"completion":"PARTIAL" if issues else "COMPLETE","output_durability":"fsync of files and output directory required; not independently authenticated","issues":issues,"provenance":"local record; non-atomic acquisition; not independently authenticated"}
 def write(name,data):
  f=os.open(name,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_CLOEXEC|os.O_NOFOLLOW,0o600,dir_fd=dfd)
  try:
   off=0
   while off<len(data):
    if test and os.environ.get("MONAD_M30_TEST_OUTPUT_WRITE_ERROR")==name and off==0: raise OSError("injected output write failure")
    chunk=data[off:]
    if test and os.environ.get("MONAD_M30_TEST_SHORT_WRITE")==name: chunk=chunk[:max(1,len(chunk)//2)]
    n=os.write(f,chunk)
    if n<=0: raise OSError("output write made no progress")
    off+=n
   if test and os.environ.get("MONAD_M30_TEST_FSYNC_ERROR")==name: raise OSError("injected fsync failure")
   os.fsync(f)
  finally: os.close(f)
 try:
  if test and os.environ.get("MONAD_M30_TEST_FAIL_AFTER_OUTPUT_CREATE")=="1": raise OSError("injected failure after output directory creation")
  data=(json.dumps(doc,sort_keys=True,indent=2)+"\n").encode()
  if len(data)>lim["output"]:
   doc={"schema":3,"acquisition_utc":stamp,"identity":ident,"administrative_allowlist":EXPECTED,"hash_allowlist":sorted(hash_allowlist),"limits":lim,"completion":"PARTIAL","issues":[{"limit_exceeded":"output_size","inventory_records_omitted":len(records),"affected_paths":EXPECTED}],"provenance":"inventory omitted because output-size limit was reached"}
   data=(json.dumps(doc,sort_keys=True,separators=(",",":"))+"\n").encode()
  if len(data)>lim["output"]: raise OSError("limit too small for minimal partial report")
  write("inventory.json",data); write("SHA256SUMS",(hashlib.sha256(data).hexdigest()+"  inventory.json\n").encode())
  if test and os.environ.get("MONAD_M30_TEST_DIR_FSYNC_ERROR")=="1": raise OSError("injected directory fsync failure")
  os.fsync(dfd)
  named=os.stat(leaf,dir_fd=output_parent_fd,follow_symlinks=False)
  if (named.st_dev,named.st_ino)!=(ds.st_dev,ds.st_ino): raise OSError("output directory replaced during acquisition; evidence remains on anchored descriptor")
 except OSError as e: print("M28.3 output failure; preserve created destination: "+str(e),file=sys.stderr); return 2
 finally:
  os.close(dfd)
  os.close(output_parent_fd)
  if test_root_fd is not None: os.close(test_root_fd)
 print(doc["completion"]+": "+out); return 0 if doc["completion"]=="COMPLETE" else 1
sys.exit(main())
PY
