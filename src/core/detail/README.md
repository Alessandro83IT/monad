# M2.5-A Linux foundations

These internal helpers are not a transaction engine. M2.5-B migrates immutable ObjectStore/generation publication only. Current,
activation, journal, integrated install/remove commits and GC remain unchanged.

`Fd::adopt` explicitly transfers ownership. `get()` borrows it; `release()`
explicitly transfers ownership out. Descriptors are move-only and close once,
without retrying Linux `close`. All opened files and directories have CLOEXEC.
Absolute directory traversal opens each component with O_DIRECTORY/O_NOFOLLOW;
leaf operations reject slash, NUL, dot and dot-dot. Since no multi-component
relative resolution is accepted, openat2 adds no material restriction to these
operations. Mount points are allowed; callers choose their authority namespace.

Mutation requires a trusted directory with no concurrent untrusted namespace
writers. A retained directory descriptor anchors operations even if ancestors
are renamed. These helpers cannot protect a caller that closes/reuses a borrowed
fd concurrently, nor defend against another writer with the same authority.

`atomic_replace` creates a random, exclusive, mode-0600 temporary in the parent,
writes completely, fsyncs the file, renames over the destination, then fsyncs
the parent. Destination symlinks are replaced as entries, never followed. Before
rename, failures leave the destination unchanged and clean up the temporary;
cleanup is best effort and cannot overwrite the primary persistence error. After
rename, any directory validation or barrier error means OUTCOME UNCERTAIN: the
new entry is visible, but recovery must not assume either crash-persistent
version. There is no rollback or false claim that the old value is restored.

`publish_noreplace` requires the caller to have already synchronized the
prepared regular file or directory tree. It performs Linux
renameat2(RENAME_NOREPLACE) in one parent and then fsyncs that parent. EEXIST
preserves both entries. Under the supported filesystem contract, other rename
errors precede visibility; a subsequent barrier error means publication is
visible but crash durability is uncertain. Unsupported renameat2/filesystems
fail explicitly; no weaker fallback is provided.

`PersistenceError` identifies Write, FileBarrier, Publication, or
DirectoryBarrier, retains errno, and independently marks uncertainty after
visibility. File and directory barriers retry EINTR and propagate every other
error. `complete_write` retries EINTR and partial progress; zero progress is
EIO. The uncertainty flag concerns namespace publication, not rollback of
arbitrary direct writes.

`Syscalls` is a per-call table for persistence syscalls and their validation,
nonce and descriptor-transfer boundaries. Production uses immutable direct Linux
bindings; tests supply local tables. It is neither a global hook nor a
filesystem mocking framework.

FileLock's registry checks all live and pending acquisitions across helper
objects and threads. Leases precede State, which precedes StoreGc; within a
rank, nested acquisition of any different lock is rejected, independent of
pathname. Inode identity detects duplicate descriptors and aliases. Caller
classification remains a contract. This conservative process-wide policy may
reject independent concurrent tasks; it avoids hidden thread-local lock-order
bypasses. Move transfers registry membership.

Lock directory and file ownership must match the effective uid, with no group or
other write permission. Lock files must be regular with one link. Parent
symlinks and symlink leaves are rejected. The lock-directory walk also requires
ancestors owned by the namespace-visible filesystem root owner or the effective
uid (root ownership may be mapped), rejecting group/other write unless the
directory is sticky (such as /tmp); subsequent child ownership is validated. The
inode is checked again after flock. Participants must never unlink/replace lock
leaves. Namespace-derived State and StoreGc paths are opt-in; the legacy
LockLayout remains unchanged. Ownership checks use namespace-visible uids. An
overflow uid cannot establish an original owner identity: namespace roots and
unmapped-owner ancestors must be trusted by the caller. These checks do not
authenticate outside-namespace writers whose identities are unavailable in the
current user namespace.

Fork shares the open file description, so locks survive until all inherited
references close. Destruction only closes, never explicitly unlocks a shared
fork description. The registry mutex has fork handlers; the child inherits
registry entries. A multithreaded child must exec or _exit before using C++
APIs, as required by POSIX. Exec closes every foundation descriptor. Inherited
owners in a single-threaded child may be destroyed normally. SIGKILL releases
the process's descriptor references through the kernel.

## Supported durability contract

Durable mutation preflights fstatfs against a conservative allowlist: the ext
family (shared EXT4 magic), XFS, Btrfs and tmpfs. All other/unknown
types fail before temporary creation or publication; this is an allowlist, not a
network-filesystem blacklist. Recognition is not proof of mount correctness.
Callers must provide local mounts with atomic Linux rename, failed rename
without remote commit, RENAME_NOREPLACE support, advisory flock and directory
fsync semantics. Overlayfs, NFS, distributed storage and arbitrary FUSE mounts
are unsupported. Overlayfs support is outside M2.5-A; backing layers and mount
configuration are not inspected. Storage must
honor barriers; unsafe mount options or dishonest hardware are outside the
guarantee. Existing ancestors must already be durable. tmpfs provides namespace
ordering only: it cannot survive reboot. Persistent records require persistent
backing storage, not tmpfs. Bind mounts do not change these rules. Only under
this contract does a rename error prove pre-visibility failure. The syscall test
seam must obey that same contract, including normal errno returns without C++
exceptions. Filesystem recognition cannot authenticate mount options, hardware
or privileged mount changes.

M2.5-B adds preflighted durable directory creation, exclusive private directories,
descriptor-relative private cleanup and two-parent tree publication. The latter
checks same-device ancestry and retains kernel EXDEV rejection (including bind
mounts); it synchronizes both parents after NOREPLACE. EEXIST synchronizes the
destination before returning to caller-owned identity verification. The unlink
seam is per-operation and only used for best-effort private cleanup.
