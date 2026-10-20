# Alpha M1: diagnosing and migrating existing Athena directories

`create_directory_tree` does not repair existing paths. It rejects a component
whose owner or permissions violate the policy, and its error includes the
component path and the failed condition. Stop Athena writers before inspecting
or changing an installation.

## Managed paths and policy

`PathLayout::from_root` defaults to `/var/lib/athena`; configuration can
override individual paths. Always inspect the effective configuration rather
than assuming the defaults.

| Path role | Default path | New leaf mode | Existing leaf policy |
| --- | --- | ---: | --- |
| Download and source cache | `/var/lib/athena/cache/downloads`, `/var/lib/athena/cache/sources` | `0755` | root/effective-user owned; no group/other write |
| Build workspace | `/var/lib/athena/build` | `0755` | root/effective-user owned; no group/other write |
| Shared package store | `/var/lib/athena/store` | `0755` | root/effective-user owned; no group/other write |
| General state | `/var/lib/athena/state` | `0700` | root/effective-user owned; exactly `0700` |
| Repository snapshots and catalog state | `/var/lib/athena/repositories/snapshots`, `/var/lib/athena/repositories/state` | `0700` | root/effective-user owned; exactly `0700` |
| Generation state | `/var/lib/athena/generations` | `0700` | root/effective-user owned; exactly `0700` |

Every traversed ancestor must be a real directory owned by root or the effective
user and must not be group/other writable. A sticky ancestor, commonly `/tmp`,
may be group/other writable if it is owned by root or the effective user. A
sticky bit alone is never sufficient: the component owner is checked, symlinks
are rejected, and the managed leaf must meet its own owner/mode policy. The
configured target root is not created by this layout initializer.

Shared leaves intentionally permit read/execute access to other users; `0755`
is not a private-state mode. Existing shared leaves with `0700` are accepted as
more restrictive, while group/other writable leaves such as `0775` are rejected.
Private state leaves require exactly `0700` so the service owner can traverse
and update them without exposing state to group/other users. In practice, a
non-root service must own these leaves; root ownership is usable when Athena
runs as root, but a `0700` root-owned directory is inaccessible to a different
effective UID.

## Compatibility with older layouts

Before Alpha M1, `std::filesystem::create_directories` created directories with
the usual `0777 & ~umask` permissions and left pre-existing directories
unchanged. Thus state paths created under umask `0002` could be `0775`; under
`0022`, `0755`; and under `0077`, `0700`. The first two are legitimate outcomes
of the old implementation but are now rejected for private state. Shared
directories created under `0002` may also be `0775`, which is rejected because
it is group-writable. The Athena root and intermediate directories such as
`repositories` were also created with that formula, so a `0775` root or
intermediate component can be rejected as an unsafe ancestor even when the
specific leaf has acceptable permissions. Directories created by root are normally root-owned;
custom layouts created by a service account are normally owned by that account.

The rejection is intentional and non-mutating. Do not recursively `chmod` or
`chown` an Athena tree based only on this compatibility note.

## Administrative diagnosis and migration

1. Stop Athena and any service or scheduled job that can write its state. Keep
   the original error text, which identifies the rejected component and reason.
2. Read the effective config and resolve each configured `PathLayout` value.
   Record which account runs Athena and the intended group-sharing model.
3. Inspect every path component from `/` to the rejected leaf without following
   symlinks. Check file type, numeric UID/GID, mode, ACLs, mount points, and
   whether a component is a bind mount or otherwise shared. For example,
   `namei -l -- PATH`, `stat -c '%F %a %u:%g %n' -- PATH`, `getfacl -p -- PATH`,
   and `findmnt -T PATH` are diagnostic aids; review their output together.
4. Treat an unexpected symlink, owner, writable ancestor, ACL grant, mount, or
   directory content as potentially compromised. Preserve the tree for review
   and investigate its provenance before changing it. Sticky `/tmp` ancestry
   does not make an attacker-owned pre-existing component trustworthy.
5. Only after confirming the exact directory is legitimate and the expected
   service owner is known, plan a narrowly scoped repair for the rejected
   component. Private leaves must end up owned by Athena's effective UID (root
   when Athena runs as root) and `0700`;
   shared cache/build/store leaves may be `0755` or more restrictive, but must
   not be group/other writable. Do not change ownership when the expected UID
   is uncertain, and do not apply a recursive mode change to package payloads.
6. Review and approve the proposed metadata changes, take a recoverable backup
   if the state warrants it, then apply only the approved changes using the
   platform's administrative tooling. This document intentionally provides no
   automatic migration command.
7. Re-run the same read-only inspection, confirm no symlinks or unexpected
   ownership remain, then restart Athena and retain its startup result.

`create_directory_tree` initializes namespace layout; it does not establish
crash durability. The descriptor-relative durable protocols remain responsible
for persistent authority and records.
