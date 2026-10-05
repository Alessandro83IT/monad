# Internal normalized artifact module

`model.hpp` is a pure Artifact Schema v1 model. `std::string` holds raw bytes
for paths, contents and link targets; it implies no text encoding. The variant
admits exactly directory, regular file and symlink values. Directory mode is
implicitly 0755, and a file's boolean implies 0644 or 0755. Lengths derive from
actual byte containers, so a declared length cannot disagree with content.
`validate` requires canonical order; `normalize` sorts and then validates.
Neither operation accesses the filesystem. Neither performs encoding/hashing.

`scan` takes an explicit root and does not infer or strip `usr`, or filter
`.athena`. Linux descriptor-relative traversal pins parents and never follows
payload symlinks. Even root locator components must be real directories.
Directory enumeration and output order use unsigned byte comparisons. Node
classification, special bits and link counts use `fstat` on no-follow handles.
Symlinks use `readlinkat` on their own O_PATH descriptors. Metadata inspection
uses fd xattrs, or `llistxattr` through a pinned `/proc/self/fd/<parent>/<name>`
locator for symlinks, plus statx and inode flag inspection. This requires procfs.

All xattrs are rejected, including Linux ACL/capability/SELinux interfaces.
An empty list does **not** establish absence of `trusted.*` without initial
user namespace CAP_SYS_ADMIN, because Linux hides those names. The scanner
rejects incomplete inspection even when visible metadata is empty. An explicit
EOPNOTSUPP from the filesystem can establish xattr absence. Capability checks
are conservative and do not accept capabilities confined to a user namespace.
Filesystem flags are inspected with FS_IOC_GETFLAGS and statx. Only explicitly
listed allocation/indexing/compression flags are ignored; unknown flags fail closed.
Symlinks require statx coverage of immutable/append flags. A tmpfs-specific
fallback requires that same coverage if its flags ioctl is unsupported;
unknown filesystems with an unavailable flags interface are rejected.

All mutation operations belong outside this module. Files/directories use
O_NOATIME, and inability to obtain that read handle fails rather than retrying
with a timestamp-changing handle. Linux can nevertheless account for symlink
access time during readlink; the scanner never writes or restores timestamps.
Atime is excluded from both the model and change detection. It does not alter
modes, ownership, contents, symlink targets, namespace membership or mtime/ctime.

Per-node identity, size, mode, ownership and mtime/ctime comparisons detect
replacement and observable changes; these values are transient and never
enter the normalized model. Directory membership is rechecked, then the
entire tree is independently read again and all observations/model bytes must
agree. This is not a kernel snapshot or a lock against hostile concurrent
writers: callers must supply a private quiescent staging tree. Later
publication must verify the published representation independently. Content
is buffered in memory (twice during re-observation); no streaming encoder,
ArtifactId, store object, manifest or execution integration exists here.

Tests use collision-free mkdtemp roots and checks active in Release as well
as Debug. Success fixtures explicitly report unavailable trusted-namespace
inspection privilege; rejection fixtures still run against the real scanner.
Optional metadata facilities are skipped only when their fixture creation
reports that the host cannot provide them. Production inspection is never
bypassed by tests.
