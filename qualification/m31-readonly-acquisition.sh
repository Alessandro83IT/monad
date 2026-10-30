#!/bin/bash
# M31 read-only evidence collector. Explicit operator invocation only.
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
PATH_ROLES = {
 "/usr/local/sbin/athena-alpha-m1-runner": ("required_installed_artifact", "installer output"),
 "/usr/local/sbin/gateway_support.py": ("required_installed_artifact", "installer output"),
 "/etc/sudoers.d/athena-alpha-m1": ("required_installed_artifact", "installer output"),
 "/var/lib/athena-alpha-m1/admin-install.lock": ("conditional_state", "created when installation locking is used"),
 "/var/lib/athena-alpha-m1": ("required_prerequisite", "protected installer state root"),
 "/var/lib/athena-alpha-m1/backups": ("required_prerequisite", "protected backup root; children are conditional"),
 "/root/athena-alpha-m1-stage": ("conditional_installer_input", "staging source, not a post-install artifact"),
 "/run/athena-alpha-m1": ("optional_volatile_runtime", "created on demand by runner"),
 "/var/log/athena-alpha-m1": ("required_installed_directory", "installer output"),
 "/var/lib/athena-alpha-m1/systemd-canary.json": ("conditional_precondition", "fresh receipt required for install; not always created by installer"),
 "/var/lib/athena-alpha-m1/ext4-fixture": ("conditional_fixture", "created by fixture preparation"),
 "/var/tmp/athena-d67-ext4": ("historical_evidence_only", "current absence does not establish historical absence"),
 "/opt/athena-qualification/build": ("required_installer_input", "authenticated build input"),
}
# The installer creates this exact lock file empty (exclusive mode 0600); that
# source-defined invariant makes its contents safe to hash. Everything else is metadata-only.
HASH_ALLOWLIST = {"/var/lib/athena-alpha-m1/admin-install.lock"}
DEFAULT = {"depth":32,"entries":10000,"total":256*1024*1024,"file":64*1024*1024,"output":8*1024*1024}
HARD = {"depth":128,"entries":100000,"total":2*1024*1024*1024,"file":512*1024*1024,"output":32*1024*1024}
FLAGS=os.O_RDONLY|os.O_DIRECTORY|os.O_CLOEXEC|os.O_NOFOLLOW

def stop(msg):
 label="IDENTITY_REFUSED" if msg.startswith("IDENTITY_REFUSED:") else "OUTPUT_NOT_DURABLE" if msg.startswith("OUTPUT_NOT_DURABLE:") else "SECURITY_BOUNDARY_REFUSED" if any(x in msg for x in ("unsafe", "symlink", "boundary", "identity/mode", "normalized", "allowlist")) else "ACQUISITION_REFUSED"
 if not msg.startswith(label+":"): msg=label+": "+msg
 print("M31 acquisition: "+msg,file=sys.stderr); raise SystemExit(2)
def os_release_field(text,key):
 values=[]
 for line in text.splitlines():
  if line.startswith(key+"="):
   value=line[len(key)+1:]
   if len(value)>=2 and value[0]==value[-1] and value[0] in ("'",'"'):
    value=value[1:-1]
   if not value or any(c not in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-" for c in value):
    raise OSError("invalid os-release field: "+key)
   values.append(value)
 if len(values)!=1: raise OSError("missing or duplicate os-release field: "+key)
 return values[0]
def meta(s):
 return {"type":stat.filemode(s.st_mode),"uid":s.st_uid,"gid":s.st_gid,"mode":format(stat.S_IMODE(s.st_mode),"04o"),"size":s.st_size,"mtime_ns":s.st_mtime_ns,"ctime_ns":s.st_ctime_ns,"inode":s.st_ino,"device":s.st_dev}
def main():
 script=sys.argv[1]; args=sys.argv[2:]
 if len(args)!=1: stop("usage: m31-readonly-acquisition.sh NEW_ABSOLUTE_OUTPUT_DIRECTORY")
 test=os.environ.get("MONAD_M31_TEST_ROOT"); lim=DEFAULT.copy()
 if test and os.geteuid()==0: stop("synthetic mode is forbidden as root")
 if not test and any(k.startswith("MONAD_M31_") for k in os.environ): stop("test configuration forbidden in production mode")
 if test:
  for k in lim:
   v=os.environ.get("MONAD_M31_LIMIT_"+k.upper())
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
  if os.environ.get("MONAD_M31_TEST_REPLACE_ROOT")=="1":
   moved=test+".anchored"; os.rename(test,moved)
   decoy=test+".decoy"; os.mkdir(decoy,0o700); os.symlink(decoy,test)
 hash_allowlist=set(HASH_ALLOWLIST)
 if test and os.environ.get("MONAD_M31_TEST_HASH_PATH"):
  synthetic_hash=os.environ["MONAD_M31_TEST_HASH_PATH"]
  if not any(synthetic_hash==p or synthetic_hash.startswith(p+"/") for p in EXPECTED): stop("synthetic hash path is outside acquisition roots")
  hash_allowlist.add(synthetic_hash)
 if test: ident={"hostname":"synthetic-debian","machine_id":"synthetic-machine-id","hardware_serial":"synthetic-virtualbox-serial","product_uuid":"synthetic-product-uuid","boot_id":"synthetic-boot-id","kernel":"synthetic-kernel"}
 else:
  def read_identity(p):
   # /etc/os-release is commonly a symlink to /usr/lib/os-release on Debian.
   # Resolve only this one identity file and pin the resolved location to the
   # canonical vendor file; all other identity inputs remain no-follow opens.
   if p=="/etc/os-release":
    link_stat=os.lstat(p)
    if stat.S_ISLNK(link_stat.st_mode):
     resolved=os.path.realpath(p)
     if resolved!="/usr/lib/os-release": raise OSError("untrusted os-release target")
     p=resolved
    elif not stat.S_ISREG(link_stat.st_mode): raise OSError("os-release is not a regular file or trusted symlink")
   fd=os.open(p,os.O_RDONLY|os.O_CLOEXEC|os.O_NOFOLLOW)
   try:
    if not stat.S_ISREG(os.fstat(fd).st_mode): raise OSError("not regular")
    return os.read(fd,65536).decode().strip()
   finally: os.close(fd)
  try:
   release=read_identity("/etc/os-release"); mid=read_identity("/etc/machine-id"); serial=read_identity("/sys/class/dmi/id/product_serial"); uuid=read_identity("/sys/class/dmi/id/product_uuid"); boot=read_identity("/proc/sys/kernel/random/boot_id"); vendor=read_identity("/sys/class/dmi/id/sys_vendor").lower(); host=os.uname().nodename
   distro=os_release_field(release,"ID"); release_version=os_release_field(release,"VERSION_ID")
   if host!="debian" or distro!="debian" or release_version!="13" or mid!="9bdbfd8678b5467cb85e4d8aa5e01fea" or serial!="VirtualBox-e475ea91-72e9-4157-9ee6-583e9ae0462f" or uuid!="91ea75e4-e972-5741-9ee6-583e9ae0462f" or not boot or not any(x in vendor for x in ("oracle","innotek")): stop("IDENTITY_REFUSED: production VM identity mismatch")
   ident={"hostname":host,"machine_id":mid,"hardware_serial":serial,"product_uuid":uuid,"boot_id":boot,"kernel":platform.release()}
  except OSError as e: stop("IDENTITY_REFUSED: cannot establish production VM identity: "+str(e))
 out=args[0]
 if not os.path.isabs(out) or os.path.normpath(out)!=out or out=="/": stop("output must be new, absolute and normalized")
 if any(out==source or out.startswith(source+"/") for source in EXPECTED): stop("output cannot be inside an acquisition root")
 repo=os.path.realpath(os.path.dirname(os.path.dirname(script)))
 if out==repo or out.startswith(repo+"/"): stop("output cannot be inside repository")
 parent,leaf=os.path.split(out)
 output_parent_path=parent
 if not leaf or leaf in (".",".."): stop("invalid output directory")
 pfd=os.open("/",FLAGS)
 dfd=None; output_parent_fd=None
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
  if not stat.S_ISDIR(ds.st_mode) or ds.st_uid!=os.geteuid() or stat.S_IMODE(ds.st_mode)!=0o700 or (ds.st_dev,ds.st_ino)!=(named.st_dev,named.st_ino): raise OSError("created output directory identity/mode mismatch")
  if test and os.environ.get("MONAD_M31_TEST_REPLACE_OUTPUT")=="1":
   os.rename(leaf,leaf+".anchored",src_dir_fd=pfd,dst_dir_fd=pfd)
   os.mkdir(leaf,0o700,dir_fd=pfd)
  output_parent_fd=os.dup(pfd)
  if test and os.environ.get("MONAD_M31_TEST_REPLACE_OUTPUT_PARENT")=="1":
   os.rename(parent,parent+".anchored"); os.mkdir(parent,0o700)
 except FileExistsError: stop("output destination already exists")
 except OSError as e:
  if dfd is not None:
   try: os.close(dfd)
   except OSError: pass
  if output_parent_fd is not None:
   try: os.close(output_parent_fd)
   except OSError: pass
  stop("OUTPUT_NOT_DURABLE: unsafe output parent or creation failure; preserve any created destination: "+str(e))
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
  finally:
   try: os.close(fd)
   except OSError as e: add_issue(path,"CLOSE_ERROR",e)
 def add_issue(path,status,error=None):
  r={"path":path,"status":status,"role":PATH_ROLES.get(path,("descendant", "within fixed acquisition scope"))[0],"role_basis":PATH_ROLES.get(path,("descendant", "within fixed acquisition scope"))[1]}
  if error is not None:r["error"]=str(error)
  records.append(r); issues.append(path)
 def mount_id(fd):
  if test and os.environ.get("MONAD_M31_TEST_MOUNT_UNAVAILABLE")=="1": return None
  try:
   with open("/proc/self/fdinfo/"+str(fd),"r",encoding="ascii") as info:
    for line in info:
     if line.startswith("mnt_id:"): return int(line.split(":",1)[1].strip())
  except OSError: return None
  return None
 def injected_boundary(path,kind):
  if not test: return False
  return os.environ.get("MONAD_M31_TEST_"+kind)==path
 def snapshot(fd,cap):
  nonlocal enum_scanned
  # Enumeration and traversal budgets are separate. Reserve one look-ahead
  # item to distinguish an exact-fit stable directory from a truncated view.
  cap=max(0,cap); names=[]; overflow=False
  with os.scandir(fd) as it:
   for entry in it:
    if len(names)>=cap: overflow=True; break
    names.append(entry.name)
    enum_scanned+=1
  names.sort(); return names,overflow
 def walk(pfd,name,path,depth,expected=None,boundary=None):
  nonlocal count,hashed
  if count>=lim["entries"]: add_issue(path,"LIMIT_ENTRIES"); return
  count+=1
  if test and os.environ.get("MONAD_M31_TEST_DISAPPEAR")==path:
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
  try:
   if test and os.environ.get("MONAD_M31_TEST_EMFILE_PATH")==path: raise OSError(24,"injected descriptor exhaustion")
   fd=os.open(name,fl,dir_fd=pfd)
  except FileNotFoundError as e: add_issue(path,"DISAPPEARED_DURING_TRAVERSAL",e); return
  except OSError as e: add_issue(path,"OPEN_ERROR",e); return
  try:
   st=os.fstat(fd)
   if test and os.environ.get("MONAD_M31_TEST_IDENTITY_MISMATCH")==path: add_issue(path,"IDENTITY_MISMATCH","injected synthetic identity mismatch"); return
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
   role,role_basis=PATH_ROLES.get(path,("descendant","within fixed acquisition scope"))
   rec={"path":path,"status":"PRESENT","metadata_before":meta(st),"role":role,"role_basis":role_basis}; records.append(rec)
   if stat.S_ISREG(st.st_mode):
    if path in hash_allowlist:
     if test and os.environ.get("MONAD_M31_TEST_HASH_ERROR")==path: rec["status"]="HASH_READ_ERROR"; rec["error"]="injected synthetic read error"; issues.append(path); return
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
     if test and os.environ.get("MONAD_M31_TEST_MUTATE_FILE")==path:
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
   if test and os.environ.get("MONAD_M31_TEST_MUTATE_DIRECTORY")==path:
    try: mutate_path(path.rstrip("/")+"/injected-entry","create")
    except FileExistsError: pass
   try:
    after=os.fstat(fd)
    names_after,overflow_after=snapshot(fd,len(names)+1)
   except OSError as e:
    # A completed first pass with no budget left for a second pass is
    # incomplete verification, not evidence that the directory changed.
    if isinstance(e,OSError): rec["status"]="TRAVERSAL_ERROR"
    rec["error"]=str(e); issues.append(path); return
   rec["metadata_after"]=meta(after)
   if not overflow and (overflow_after or names!=names_after or (st.st_dev,st.st_ino,st.st_mtime_ns,st.st_ctime_ns)!=(after.st_dev,after.st_ino,after.st_mtime_ns,after.st_ctime_ns)):
    rec["status"]="DIRECTORY_CHANGED_DURING_ACQUISITION"; issues.append(path)
   elif overflow:
    rec["verification_status"]="INCOMPLETE_ENTRY_BUDGET"
  finally:
   try: os.close(fd)
   except OSError as e: add_issue(path,"CLOSE_ERROR",e)
 for path in EXPECTED:
  target=mapped(path); parent,name=os.path.split(target)
  try:
   fd=open_chain(parent)
   try:
    try: s=os.stat(name,dir_fd=fd,follow_symlinks=False)
    except FileNotFoundError as e:
     if path in OPTIONAL: records.append({"path":path,"status":"OPTIONAL_ABSENT","operation":"stat","role":PATH_ROLES[path][0],"role_basis":PATH_ROLES[path][1]})
     elif PATH_ROLES[path][0] in ("conditional_state","conditional_installer_input","conditional_precondition","conditional_fixture"):
      records.append({"path":path,"status":"CONDITIONAL_ABSENT","operation":"stat","role":PATH_ROLES[path][0],"role_basis":PATH_ROLES[path][1],"qualification_relevance":"depends on applicable installer or fixture phase"})
     elif PATH_ROLES[path][0]=="historical_evidence_only":
      records.append({"path":path,"status":"HISTORICAL_TARGET_CURRENTLY_ABSENT","operation":"stat","role":PATH_ROLES[path][0],"role_basis":PATH_ROLES[path][1],"historical_absence_inferred":False})
     else:
      records.append({"path":path,"status":"REQUIRED_ABSENT","operation":"stat","error":str(e),"role":PATH_ROLES[path][0],"role_basis":PATH_ROLES[path][1],"historical_absence_inferred":False})
      issues.append(path)
     continue
    if stat.S_ISLNK(s.st_mode): records.append({"path":path,"status":"SYMLINK_NOT_FOLLOWED","metadata":meta(s),"role":PATH_ROLES[path][0],"role_basis":PATH_ROLES[path][1]}); issues.append(path)
    else: walk(fd,name,path,0,s)
   finally:
    try: os.close(fd)
    except OSError as e: add_issue(path,"CLOSE_ERROR",e)
  except FileNotFoundError as e: add_issue(path,"MISSING_INTERMEDIATE_DIRECTORY",e)
  except OSError as e: add_issue(path,"PATH_ACCESS_ERROR",e)
 stamp=datetime.datetime.now(datetime.timezone.utc).isoformat()
 doc={"schema":4,"acquisition_utc":stamp,"identity":ident,"administrative_allowlist":EXPECTED,"hash_allowlist":sorted(hash_allowlist),"limits":lim,"entry_count":count,"bytes_hashed":hashed,"results":records,"completion":"PARTIAL" if issues else "COMPLETE","output_durability":"fsync of files and output directory required; not independently authenticated","issues":issues,"provenance":"local record; non-atomic acquisition; not independently authenticated"}
 def write(name,data):
  f=os.open(name,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_CLOEXEC|os.O_NOFOLLOW,0o600,dir_fd=dfd)
  try:
   off=0
   while off<len(data):
    if test and os.environ.get("MONAD_M31_TEST_OUTPUT_WRITE_ERROR")==name and off==0: raise OSError("injected output write failure")
    if test and os.environ.get("MONAD_M31_TEST_ZERO_WRITE")==name: n=0
    else:
     chunk=data[off:]
     if test and os.environ.get("MONAD_M31_TEST_SHORT_WRITE")==name: chunk=chunk[:max(1,len(chunk)//2)]
     n=os.write(f,chunk)
    if n<=0: raise OSError("output write made no progress")
    off+=n
   if test and os.environ.get("MONAD_M31_TEST_FSYNC_ERROR")==name: raise OSError("injected fsync failure")
   os.fsync(f)
  finally: os.close(f)
 try:
  if test and os.environ.get("MONAD_M31_TEST_FAIL_AFTER_OUTPUT_CREATE")=="1": raise OSError("injected failure after output directory creation")
  data=(json.dumps(doc,sort_keys=True,indent=2)+"\n").encode()
  if len(data)>lim["output"]:
   doc={"schema":4,"acquisition_utc":stamp,"identity":ident,"administrative_allowlist":EXPECTED,"hash_allowlist":sorted(hash_allowlist),"limits":lim,"entry_count":count,"bytes_hashed":hashed,"results":[],"completion":"PARTIAL","output_durability":"fsync of files and output directory required; not independently authenticated","issues":[{"limit_exceeded":"output_size","inventory_records_omitted":len(records),"affected_paths":EXPECTED}],"provenance":"local record; non-atomic acquisition; not independently authenticated"}
   data=(json.dumps(doc,sort_keys=True,separators=(",",":"))+"\n").encode()
  if len(data)>lim["output"]: raise OSError("limit too small for minimal partial report")
  write("inventory.json",data); write("SHA256SUMS",(hashlib.sha256(data).hexdigest()+"  inventory.json\n").encode())
  if test and os.environ.get("MONAD_M31_TEST_DIR_FSYNC_ERROR")=="1": raise OSError("injected directory fsync failure")
  os.fsync(dfd)
  # Re-resolve the supplied parent with no-follow component opens. The output
  # is written through output_parent_fd, but a renamed parent makes the named
  # destination ambiguous and must not be reported as a successful acquisition.
  check_parent=os.open("/",FLAGS)
  try:
   for component in output_parent_path.split("/")[1:]:
    if not component: continue
    nxt=os.open(component,FLAGS,dir_fd=check_parent); os.close(check_parent); check_parent=nxt
   expected_parent=os.fstat(output_parent_fd); named_parent=os.fstat(check_parent)
   if (expected_parent.st_dev,expected_parent.st_ino)!=(named_parent.st_dev,named_parent.st_ino): raise OSError("output parent replaced during acquisition; evidence remains on anchored descriptor")
  finally: os.close(check_parent)
  named=os.stat(leaf,dir_fd=output_parent_fd,follow_symlinks=False)
  if (named.st_dev,named.st_ino)!=(ds.st_dev,ds.st_ino): raise OSError("output directory replaced during acquisition; evidence remains on anchored descriptor")
 except OSError as e: print("M31 OUTPUT_NOT_DURABLE; preserve created destination: "+str(e),file=sys.stderr); return 2
 finally:
  close_error=None
  for owned_fd in (dfd,output_parent_fd,test_root_fd):
   if owned_fd is not None:
    try: os.close(owned_fd)
    except OSError as e: close_error=e
  if close_error is not None:
   print("M31 OUTPUT_NOT_DURABLE: descriptor close failed: "+str(close_error),file=sys.stderr)
   return 2
 print(doc["completion"]+": "+out); return 0 if doc["completion"]=="COMPLETE" else 1
sys.exit(main())
PY
