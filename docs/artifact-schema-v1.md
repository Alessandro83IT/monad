# Athena Artifact Schema v1

## Status, purpose and scope

This is the normative M2.2-B.2 specification under
[ADR-001](adr/0001-build-input-artifact-identity-content-addressed-store.md).
MUST, MUST NOT, SHOULD and MAY have their usual normative meanings. It defines
one normalized realized filesystem output, its canonical encoding, and the
future derivation of ArtifactId. The schema/model is not itself ArtifactId.
This document implements no scanner, encoder, hashing or publication.

Version 1 targets Linux-like POSIX package payloads, including Noesix/Linux
byte filenames. It does not represent Windows drives, alternate streams,
case-insensitive lookup, reparse points or Windows security descriptors.
Artifact equality is equality under these normalization rules, not a proof of
behavioral equivalence, authenticity, safe activation or reproducibility.

The contract is written first; no new public core type or runtime data model is
required in B.2. A subsequent internal artifact module SHOULD implement it.
The artifact schema version is independent of the existing ArtifactMetadata
TOML envelope version and of core::SchemaKind::Manifest. Existing opaque public
ArtifactId values retain their B.1 meaning; they are not retrospectively
validated or reinterpreted as locally verified v1 identities.

## Terminology and root boundary

The **payload root** is the selected directory containing exactly the output
being identified. A **raw tree** is that output before validation and declared
normalization. The **normalized model** contains schema version 1 and all
supported entries below the root. **Canonical bytes** are its encoding below.
**Administrative metadata** describes storage, identity or provenance outside
the payload boundary; it is not a payload entry.

The root MUST be a real directory, not a symlink. It is implicit and has no
entry, path field or variable metadata in the encoding. Its logical mode is
0755. Every directory below it MUST have an explicit entry, including empty
directories. Each entry has a canonical root-relative path and exactly one
type-specific value. Top-level entries have the implicit root as parent.

Selecting the payload root is an execution/output-layout responsibility, not
an inference made from package name, store location or tree contents. In
particular, this schema does not mandate stripping `usr` from a staging tree.
The same tree relocated to another realization/store directory has the same
representation. No absolute root locator is encoded.

There are no silently excluded payload names: `.athena` inside the selected
payload is ordinary payload and MUST be represented. Future administrative
records MUST live outside that boundary; an ID record MUST NOT recursively
encode its own ID. Package-specific provenance MUST remain separate from the
shared payload. Legacy object roots with embedded `.athena` records cannot be
adopted by silently filtering them as if they were already v1 payloads.

## Paths and byte model

Paths are nonempty byte strings. Separator is exactly byte 0x2f (`/`). A path
MUST consist of nonempty components separated by single separators. It MUST
NOT contain NUL (0x00), begin or end with `/`, contain repeated separators, or
contain a component exactly `.` or `..`. Empty path and `.` do not encode the
root. Absolute paths and traversal are rejected, not repaired. No collapsing,
lexical normalization, percent decoding, escaping interpretation or trimming
is performed at the model boundary.

Components support any other bytes, including invalid UTF-8, newline, colon
and backslash. Backslash is an ordinary filename byte, not a separator. Case
is preserved; Unicode normalization and locale-dependent case folding are
forbidden. Canonically composed and decomposed Unicode names remain distinct.
Linux treats a name such as `C:foo` as an ordinary relative component; this is
not a Windows path contract. Human-readable formats MUST preserve arbitrary
bytes through an explicit reversible representation, not lossy Unicode.

For example, `bin/tool` and the bytes `64 61 74 61 2f ff` are valid paths.
`/bin/tool`, `bin//tool`, `bin/./tool`, `bin/../tool`, `bin/`, empty bytes and
any embedded NUL are invalid. `.config` and `a..b` are valid components.

## Entry types and file contents

Only directory, regular file and symbolic link are supported. FIFOs, sockets,
block/character devices and every other node type MUST cause rejection of the
artifact, never omission or conversion. A symlink MUST be identified without
following it, even when it points to a regular file or directory.

A regular-file entry encodes its path, type, normalized executable state and
complete logical content bytes, including zero bytes and an empty content
sequence. There is no text/newline conversion. File length is the byte count.
Sparse allocation, physical extents, compression and disk usage are not
semantic: holes contribute the same logical zero bytes as allocated zeros.
Unreadable/incompletely observed content MUST fail validation, not become empty
content. Inode/device numbers are observation aids only, never encoded.

Inline raw bytes are the v1 content representation. They support streaming
without buffering an entire file and avoid a second digest algorithm/domain
contract or trusting unverified per-file digests. Canonical bytes can be large;
they need not be retained as a manifest file. A digest-only model is not a v1
model. Optional inspection/independent-verification digests in a future store
manifest MUST be separately specified and verified; they cannot replace these
content bytes in ArtifactId derivation.

## Directories, modes and permissions

Directory entries encode path and type only. Their logical mode is always
0755; their presence preserves structure and empty directories. Raw ordinary
directory permission bits normalize to this mode, including on the root.

For regular files, executable state is true exactly when any of the raw POSIX
0111 bits is set. False normalizes to logical mode 0644, true to 0755. The
model stores only this boolean, not the original nine permission bits.
Read/write bits and distinctions between owner/group/other execute bits are
explicitly normalized; they do not encode umask or build-user access classes.
For example 0600 and 0644 converge; 0700, 0750 and 0755 converge; 0644 and
0755 differ. Directory 0700 and 0755 converge.

Setuid (04000), setgid (02000) and sticky (01000) are unsupported and MUST cause
rejection on every observed node, including the implicit root. They MUST NOT
be masked away. Symlink ordinary permission bits have no v1 semantic field
under Linux; symlink chmod behavior on other platforms is outside scope.

This normalization declares a publicly readable package-output model; it does
not support payloads requiring private read/write access classes, per-user
execution permissions, writable shared directories or special permissions.
An execution request declaring such requirements MUST be rejected. Inspection
of raw ordinary mode bits alone does not infer that intent. Normalization MUST
be applied to the published logical payload, not merely omitted from hashing
while leaving divergent access semantics in the output. Later publication MAY
remove write permissions as immutable-store hardening according to its own
explicit contract; such hardening is not a new semantic mode or ArtifactId.
No physical finalization is implemented here.

## Symbolic links

A symlink entry encodes its own canonical path and a nonempty, NUL-free target
byte string exactly as stored by the filesystem. Targets use the same raw byte
model as filenames but do not obey canonical entry-path restrictions.
Targets MUST NOT be resolved, followed, rewritten or normalized.

Absolute targets, dangling links, cycles, `.`/`..`, repeated separators and
trailing separators are allowed in targets and preserved literally. A target
MAY escape the payload root. `../lib/tool`, `/usr/bin/tool`, `a//b` and `a/b`
are distinct target byte strings. The schema describes the link, not its
referent or the safety/existence of that referent.

Identity processing MUST NOT traverse links, including in parent components.
All entry parents must be directory entries. Later activation/security policy
MUST decide which external references are permitted and how runtime references
remain reachable; v1 identity acceptance alone authorizes no activation.
Literal absolute targets containing temporary/store paths affect identity as
payload bytes do: they are not replaced with an inferred portable target.
Thus relocation itself has no effect, but rewriting a target or file content
to embed a different root is a payload change.

## Hard links

Hard-link topology is unsupported in v1. Every raw regular file MUST have link
count exactly one. Count greater than one MUST cause rejection, even if the
other link is outside the payload. Directory link counts are not subject to
this rule; symlink inode sharing is also unsupported and MUST be rejected if
its link count exceeds one. No inode/device identity or link count is encoded
after validation.

Explicit topology would require extra relationship and mutation contracts.
Normalizing linked files into independent identical entries could change
observable aliasing and mutation behavior; future immutability alone does not
justify doing so silently. Archives may realize hard links and therefore
produce unsupported trees. Extraction MUST NOT hide topology by copying first
and validating only the copies. Independent regular files with equal contents
are valid and remain separate entries; neither equality nor deduplication
implies a hard-link relationship. Disk usage is not an identity field.

## Ownership, timestamps and extended metadata

uid, gid and user/group names are normalized away. No named owner or numeric
owner participates in the model. Logical payload ownership is unspecified;
publication ownership is administrative hardening. Raw root/non-root builder
ownership therefore does not change identity. Required owner-specific behavior
is unsupported and MUST be rejected when declared, rather than approximated.

mtime, atime, ctime and birth time are ignored for identity on every node,
including the root. They MUST NOT be encoded. v1 supplies no semantic timestamp
and no promise to preserve one. A declared requirement for timestamp-dependent
payload behavior is unsupported and MUST fail. Numeric timestamp normalization
for materialization, if needed, belongs to a later explicit execution contract.

All xattrs, in every namespace (including user, trusted, security and system),
are unsupported: any present attribute, even with an empty value, MUST cause
rejection. Nontrivial access ACLs and all default ACLs MUST cause rejection;
the base owner/group/other access entries equivalent to ordinary mode bits are
covered by mode normalization. Linux capabilities, SELinux labels and other
security metadata are unsupported and MUST be rejected, whether exposed as
xattrs or by another interface. This includes labels added by the host; v1
does not silently strip them. These requirements include root and symlinks.

Other observable filesystem flags/metadata that alter access, execution,
inheritance, mutation or interpretation (for example immutable/append-only
flags) MUST cause rejection. Nonsemantic allocation/accounting information is
ignored. A future scanner MUST establish absence of unsupported metadata;
inspection failure or inability to establish support MUST fail closed, not
claim successful v1 verification. A filesystem explicitly reporting that it
cannot store a metadata category can establish absence for that category.
The current extraction/copy code is not evidence of such validation.

## Canonical ordering and model validation

Entries MUST be sorted lexicographically by their full canonical path bytes,
comparing bytes as unsigned values 0..255. At the first different byte, the
smaller sorts first; an exact prefix sorts before the longer path. No locale
collation or signed-char comparison is allowed. Equal paths are duplicates
and MUST be rejected, even if their fields match; there is no tie breaker.
Unordered input models MAY be sorted before encoding, after duplicate checking.
Canonical byte decoders MUST reject out-of-order entries.

A model validator MUST require:

- Schema version exactly 1 and only the three supported entry types.
- Canonical, unique byte paths; every non-top-level parent explicitly present
  as a directory, and no descendants of regular files or symlinks.
- Exactly the fields for the entry type: directory has no variable metadata;
  file has boolean executable state and complete content; link has literal
  nonempty NUL-free target. Unknown fields/semantic extensions are rejected.
- Content length equal to actual bytes, valid lengths/counts in the encoding
  range, and deterministic ordering after canonicalization.
- No unsupported metadata or topology represented as accepted semantics.

A pure normalized model cannot prove that a filesystem was inspected faithfully
or that raw dangerous bits/metadata were absent. Raw-tree validation MUST precede
normalization; its evidence stays outside canonical bytes. A model alone is
not a locally verified ArtifactId. Later scanning must validate the root and
every node, avoid symlink traversal, detect incomplete/changing observations,
and ensure encoded content is the normalized content later published. An
unverifiable or changing tree MUST fail identity generation. Filesystem
validation and observation mechanisms are deferred to B.3, not implemented
by these written model rules.

## Canonical byte encoding v1

The following is the complete grammar. `||` means byte concatenation; it adds
no separator. `U8(n)` is one unsigned byte. `U32(n)` and `U64(n)` are exactly
four and eight unsigned big-endian bytes. Values MUST fit their respective
ranges; overflow is rejected, never wrapped. `B(s) = U64(byte_length(s)) || s`.
Strings are bytes, with no terminator, BOM, Unicode conversion or padding.
Quoted markers below are exact ASCII bytes; `\0` denotes one 0x00 byte.

```text
C = "athena-artifact-tree\0" || U32(1) || U64(entry_count)
    || E[0] || ... || E[entry_count - 1]

directory E = U8(0x01) || B(path)
file      E = U8(0x02) || B(path) || U8(executable) || B(content)
symlink   E = U8(0x03) || B(path) || B(target)
```

`executable` MUST be exactly 0x00 or 0x01. Entries follow canonical ordering.
There are no optional fields, per-file digest slots, mode integers, alignment
bytes, compression, textual numeric formatting or end marker. Framing by
counts, type tags and lengths is unambiguous even when content contains markers,
NULs or arbitrary binary data. A decoder MUST reject unknown markers, versions,
tags, invalid boolean bytes, malformed/truncated lengths, count mismatches,
noncanonical models and trailing bytes. Local resource limits MAY reject an
otherwise representable model explicitly; they MUST NOT truncate it or change
the encoding. The normative range is 0..2^64-1 for counts and byte lengths.

An encoder MAY stream each file after its length; it MUST supply precisely that
many verified bytes. C is independent of traversal/repository order, process
IDs, pointers, locale, timestamps and absolute realization/store locators.
There is no JSON/TOML formatting dependency. Changes to these rules require a
different artifact schema/encoding version, not a formatter update.

## ArtifactId derivation contract

The future derivation is exactly:

```text
raw realized tree -> validation and declared normalization
                  -> Artifact Schema v1 model -> C
P = "athena-artifact\0" || U32(1) || U64(byte_length(C)) || C
digest = SHA-256(P)
ArtifactId text = "sha256:" || lowercase_hex(digest)
```

The outer domain marker is exactly 16 ASCII bytes including the final NUL;
the inner tree marker is exactly 21 bytes including the final NUL. The schema
version deliberately occurs in both frames and MUST agree. Hexadecimal text
contains all 32 digest bytes as exactly 64 lowercase digits; it is not hashed
as text. Computing the framed length can use validated lengths before streaming;
overflow of the outer U64 length MUST be rejected. No ArtifactId is calculated
in B.2.

This domain identifies normalized output only. It MUST NOT be reused for
BuildInputId, acquired sources, transport archives, per-file digests, repository
snapshots or a serialized manifest-file digest. This specification defines no
additional output_digest/manifest_digest algorithm or new interpretation of
existing ArtifactMetadata fields. Their future production/verification and
mapping to the v1 scheme require a separate explicit integration contract;
they MUST NOT be inferred merely from equal digest strings. Imported identity
claims MUST be verified locally against P before being treated as v1 identities.
Any validation, encoding or hashing failure MUST prevent identity assignment.

## Empty tree and encoding examples

An empty artifact is valid: it is an implicit root directory and zero entries.
Its one canonical encoding is exactly these 33 bytes:

```text
61 74 68 65 6e 61 2d 61 72 74 69 66 61 63 74 2d 74 72 65 65 00
00 00 00 01
00 00 00 00 00 00 00 00
```

Its derivation prefix is the outer marker, `00 00 00 01`, and
`00 00 00 00 00 00 00 21` (decimal 33 encoded as hexadecimal 0x21), followed
by those C bytes.
An empty directory entry named `empty` changes entry count to 1 and appends:

```text
01 00 00 00 00 00 00 00 05 65 6d 70 74 79
```

A top-level non-executable file `a` with content bytes `00 ff` is:

```text
02 00 00 00 00 00 00 00 01 61 00 00 00 00 00 00 00 00 02 00 ff
```

A top-level symlink `l` with literal target `../a` is:

```text
03 00 00 00 00 00 00 00 01 6c 00 00 00 00 00 00 00 04 2e 2e 2f 61
```

In a combined tree, paths `a`, `empty`, `l` appear in that order regardless
of enumeration. `bin/tool` requires a directory entry `bin`; adding it alone
without `bin` is invalid. `bin/tool` cannot be a descendant of symlink `bin`.
The model `{directory "bin", executable file "bin/tool" with bytes "hi\n"}`
orders `bin` first, then `bin/tool`, and encodes content as `68 69 0a` of length
3. Byte paths `7a` and `ff` order in that sequence regardless of locale.
Two independent files containing the same bytes are valid; linking their
inodes makes the raw tree unsupported. These examples specify bytes only,
not digest test vectors or executable tests.

## Manifests, existing contracts and inspection findings

Canonical encoding and human-readable/store manifests MUST be separate
representations derived from the same validated normalized model. A future
inspection manifest MUST faithfully describe paths, types, modes, targets and
content verification information without making its formatting an ArtifactId
input. It MAY evolve independently if its model meaning stays intact. It
cannot establish payload integrity without verification of the actual payload.
Its format, digest domain and administrative placement remain deferred.

Repository inspection at checkpoint `43b32d1` found:

| Existing structure/behavior | Classification and reuse boundary |
| --- | --- |
| `src/store/manifest.{hpp,cpp}`: `ManifestEntry` with string path; `.athena/manifest.toml` `[[files]]` tables | A: installation bookkeeping; B: ownership/removal/activation list. Not C: artifact identity. Paths are a conceptual input only; the type/codec lacks v1 semantics and is not reusable as the canonical model. |
| `src/store/store.cpp`: staged `usr` traversal, regular-file predicate, relative paths, `copy_file` | A/B: legacy realization/copying. Omits directory entries and can follow a file symlink through its regular-file predicate/copy. No v1 raw-tree validation; MUST NOT be used as normalization. |
| `src/store/metadata.{hpp,cpp}` and package TOML loader/model | A/B: package name/version, source checksum, build system, dependencies and capabilities; package association/removal inputs, not C. No payload content identity. |
| `src/generation/generation.{hpp,cpp}`, generation builder and GC | A/B: package plus store path, numeric generation IDs and reachability. Absolute-path normalization/sorting serves legacy lookup, not canonical payload paths. |
| Public `ArtifactId`, `ArtifactMetadata`, `DigestSet` | C: identity/claim metadata boundary, not a filesystem model or proof of verification. D: retain typed IDs and distinct provenance/identity associations; no public schema expansion needed now. |
| `src/archive/archive.cpp`, Arch extraction; CMake/Autotools backends | System tar extraction and ambient DESTDIR installation do not define normalized permissions, links, ownership or security metadata. Reusable acquisition/build components only, subject to future validation. |
| `src/verify/verifier.cpp`, Arch verification, `src/core/repository.cpp` | Transport SHA-256 uses system helpers; snapshot hashing uses EVP and TOML material. SHA-256 machinery may be reusable internally later, but their materials/domains/formatters are not v1 encoding. |
| `src/core/resolution_order.hpp`, repository candidate sorting, legacy filesystem sorting | Determinism for other contracts; no existing artifact-byte ordering/encoder. The sorting technique is reusable, its keys are not. |

Repository-wide inspection found no implemented canonical filesystem artifact
model, hard-link preservation contract, xattr/ACL/capability validation or
explicit normalized file/directory mode model. Current native build semantics
justify a minimal executable/non-executable package model, not arbitrary POSIX
metadata support. The legacy store does not fulfill this specification.

Related tests cover legacy activation and duplicate-path rollback, generation
switching/rollback and merging, removal and historical GC reachability, package
and store metadata compatibility, CMake staging and installer extraction, public
artifact origin/opaque IDs/codecs, and repository snapshot ordering/digests
(`activation`, `generation`, `generation_builder`, `removal_e2e`, `gc`,
`store_metadata`, `package_loader`, `cmake_backend`, `installer_e2e`,
`core_foundation`, `repository_api_types`, `artifact_contract`,
`metadata_schema`, `repository_snapshot` tests; Arch extraction/transport checks
also appear in repository tests/live smoke coverage). They are evidence for
their existing contracts, not tests of v1 filesystem normalization or encoding.
No existing legacy manifest is reinterpreted, replaced or migrated by B.2.

## Deferred work and implementation boundary

B.3 can begin implementing internal model validation and supported raw-tree
scanning/normalization under these rules. It must establish faithful byte paths,
no-follow observation, metadata absence, topology checks and stable contents.
B.4 must implement canonical encoding/hash verification with independent exact
byte fixtures and failure handling. No public scanner API is implied.

Remaining integration work includes explicit output-root selection, readable
manifest codec and digest contracts, ArtifactMetadata scheme association,
immutable publication/finalization and concurrency, candidate/provenance
associations, activation/external-link policy and reachability, and eventual
dual legacy/new generation/GC readers. A publication contract must verify its
final hardened representation against the same logical normalization rules.

Advanced permissions, semantic ownership/timestamps, security metadata, special
nodes, hard-link topology, multiple outputs and cross-platform filesystem
semantics are excluded from v1; adding them requires an explicit new version.
This specification changes no resolver, InstallRequest, ResolutionResult,
plan_install(), acquisition/build, installer/CLI, legacy store or generation
behavior. Filesystem scanning, artifact hashing, publication, InstallPlan
execution and all migrations are outside B.2. ADR-001 remains unchanged.
