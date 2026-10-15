# Private Athena TOML fork, upstream toml++ 3.4.0

Copyright Mark Gillard; upstream https://github.com/marzer/tomlplusplus/tree/v3.4.0.
The original MIT license is reproduced in LICENSE; original attribution/SPDX
comments remain in every derived header. Athena's observer patch and mechanical
isolation are local modifications, not an upstream release.

This directory vendors the **complete** 51-header distribution tree. It does not
fall through to system implementation headers. `UPSTREAM.sha256` records every
original header used, before patching/transformation. `observer.patch` records the
three original overlay changes relative to those headers, before isolation.

`vendor.py /path/to/original/toml++` verifies every original SHA-256, applies the
observer patch without fuzz in a temporary directory, performs the deterministic
transform, then compares every generated header. `--write` regenerates the headers.
No download occurs. The pinned source version assertion remains mandatory.

The token-aware transform replaces the `toml` identifier with `athena_toml`, all identifiers
starting `TOML` with `ATHENA_TOML`, and the old include-guard alias; include paths
and the main header name also change. This covers namespaces, friends, template
arguments/specializations, inline definitions, implementation symbols, macros and
header guards. Comments and string/character/raw literals are preserved, including
upstream diagnostic wording; only include header names are rewritten. There
is no namespace alias from `toml` to the fork and no shared AST representation.

Athena-internal codecs use `<athena_toml/athena_toml.hpp>` and `athena_toml::`.
`third_party` is a PRIVATE core include directory, is absent from public consumer
compile flags, and is not installed. No installed public header mentions fork
types. Existing independent config/package/manifest loaders retain the original
system parser. Original and fork can occur in the same TU in either include order.
No internal AST crosses the installed public Athena API.

## Upgrade review

1. Obtain/review the new upstream tree and license, and record exact source hashes.
2. Review upstream changes to every allocation, key/string growth, implicit/dotted
   table, array element and recursive descent site. Port observer.patch; do not
   drop hooks just to make the patch apply. Review required-field completion rules
   against the model codecs, including historical formats.
3. Review the complete namespace/macro transform: new guards, helper namespaces,
   extern/inline functions, friends, std specializations and include paths must
   remain isolated. Check both header include orders and the original consumer.
4. Regenerate, then run independent type/value/acceptance differential fixtures,
   every typed-profile fixture, malformed/budget cases, H1/H2/H3 and D8 regressions.
5. Run clean Debug/Release qualification and structural/object-symbol checks;
   retain manifests, compiler/configuration, commands and individual results.
6. Only then update version assertions and UPSTREAM.sha256. Do not disable the
   version assertion to accept an unreviewed system release.

The tests provide finite empirical and structural evidence, not a proof against
all possible ODR misuse by external code. They do not qualify alternate macro
configurations, compilers/platforms or a global RSS/time ceiling. Source-derived
budgets and the per-container context map preserve the documented memory/time
limits; they are not process isolation or a hardware-safe heap quota.
