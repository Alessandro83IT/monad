# Monad Package Manager
## User manual and contributor guide

First English edition · 31 October 2026 · Documented baseline: `0.1.0-alpha.1`

**Status: experimental-version documentation.** This manual translates the Italian edition and describes the source archives supplied for review on 30 October 2026. It does not certify a release, Alpha M1 qualification, or the behavior of later revisions. Commands were checked against the source; they were not executed during preparation because CMake was unavailable in the documentation environment.

Languages: [Italian manual](manuale-it.md) · English manual

## Contents

1. [What Monad is](#1-what-monad-is)
2. [Features and limitations](#2-features-and-limitations)
3. [Core concepts](#3-core-concepts)
4. [Building](#4-building)
5. [First run](#5-first-run)
6. [Configuration and paths](#6-configuration-and-paths)
7. [Your first local package](#7-your-first-local-package)
8. [Command reference](#8-command-reference)
9. [Generations and rollback](#9-generations-and-rollback)
10. [Integrity and execution model](#10-integrity-and-execution-model)
11. [Troubleshooting](#11-troubleshooting)
12. [Contributor guide](#12-contributor-guide)
13. [Technical documentation map](#13-technical-documentation-map)
14. [Sources and editorial verification](#14-sources-and-editorial-verification)

## 1. What Monad is

Monad Package Manager is an experimental Linux package manager written in C++20. Its command-line program is named `mpkg`. The project develops a pipeline connecting package metadata, dependency resolution, source compilation, artifact storage, and generation activation.

The project direction combines source builds inspired by Gentoo/Portage, declarative configuration inspired by Nix, immutable storage, and generations. Integration with the Arch ecosystem is a goal. These goals do not establish compatibility or completeness in the documented version.

Monad is the package manager underpinning the Henosix OS project. This manual covers Monad; it does not document a Henosix installer, a complete bootable system, or distribution administration procedures.

C++ namespaces, files, and some messages still use the previous name, `athena`. The public CLI name used here is `mpkg`. Do not manually rename persistent state to match the new name.

## 2. Features and limitations

| Area | Status in the documented baseline |
|---|---|
| Local catalog | Reads TOML manifests under `repositories/state/packages` |
| Dependencies | Resolution through the public model and an installation plan |
| Executable source recipes | One local source with a literal `file:///absolute/path` URI |
| Archive | Restricted gzip-compressed tar subset with one top-level directory |
| Build | CMake, without additional recipe build parameters |
| Source digest | SHA-256 verification when provided in metadata |
| Modern storage | Objects addressed by a digest of installed content |
| Installed state | Persistent records retaining dependency semantics |
| Generations | CLI listing, current-state inspection, switch, and rollback |
| Removal | Validates the resulting environment before activation |
| Garbage collection | Internal mechanisms; no public `mpkg gc` command |
| Remote sources and multiple-source recipes | Unsupported by the current realization path |
| Autotools, binary import, Athena IR execution | Unsupported by the current realization path |
| Sandboxing and reproducible builds | No guarantees provided by the current executor |
| Replacing pacman, emerge, or Nix | Not ready for dependable production use |

The presence of a type, historical backend, or module in the source does not mean that the CLI exposes it or that the modern pipeline executes it. An isolated passing test is not qualification of the complete product.

## 3. Core concepts

**Package and candidate.** A package has a name and version. A candidate also preserves repository and snapshot identity. Two selections with the same visible name are not necessarily identical complete selections.

**Recipe and plan.** A recipe describes how to acquire and build a source. The resolver selects candidates and prepares a plan; realization consumes the plan's selections. It must not replace them by selecting again from the catalog during a build.

**Artifact.** This is the installed staging output, inspected according to the artifact schema. Its `ArtifactId` uses SHA-256 and describes normalized output. The source archive digest describes archive bytes instead; these digests serve different purposes.

**ObjectStore.** Modern objects reside under `objects/v1/sha256/<digest>`, relative to the configured store root. Multiple records can share identical objects. Do not edit store files or metadata directly.

**Realization.** A record connects an exact selection to its verified artifact and retains required installed-state information. Current ambient builds do not provide evidence of reproducibility or a complete identity of build inputs.

**Generation.** A generation records a set of package memberships. Its numeric ID is distinct from candidate identity and artifact digest.

**Activation.** Output is projected into the configured target through links and directories. The trial target is a directory below the experimental root. Activating it does not automatically add programs to the shell's `PATH`.

## 4. Building

Requirements: Linux, a C++20 compiler, CMake 3.20 or newer according to the declared requirement, Ninja or Make, OpenSSL and LibArchive development libraries, pthreads, and Python 3 for parser and packaging checks. Packaging tests also require `readelf`, `ldd`, and `mktemp`.

From a complete project checkout, use a new build directory:

```bash
cmake -S . -B build-manual -DCMAKE_BUILD_TYPE=Release
cmake --build build-manual --parallel
./build-manual/mpkg --version
./build-manual/mpkg --help
```

The internal CMake executable target is still named `athena`; its output filename is `mpkg`. The baseline declares version `0.1.0-alpha.1`.

After building the test targets as well, run tests without the privileged label:

```bash
ctest --test-dir build-manual --output-on-failure -LE privileged
```

Even a test without the privileged label may skip coverage because of filesystem inspection limits. Record PASS, FAIL, SKIP, TIMEOUT, and NOT RUN separately; a SKIP is not a PASS.

To try installing Monad's own files into a dedicated user prefix after the build:

```bash
cmake --install build-manual --prefix "$HOME/monad-manual-prefix"
"$HOME/monad-manual-prefix/bin/mpkg" --version
```

Use a new prefix that is not shared with other installations. Preserve the relative binary/library layout. This installs the Monad program; it differs from `mpkg install`, which manages packages within its configured environment.

## 5. First run

Run trials as a normal user in a fresh, disposable root. `--root` relocates package-manager state and the activation target; it does not isolate build code.

From the checkout root, in the same shell:

```bash
trial_root="$(mktemp -d /tmp/monad-manual-XXXXXXXX)"
./build-manual/mpkg --root "$trial_root" config check
./build-manual/mpkg --root "$trial_root" paths
./build-manual/mpkg --root "$trial_root" list
./build-manual/mpkg --root "$trial_root" generation list
```

`paths` exists as a diagnostic command but is omitted from general help. An empty root may have no initialized generation; the corresponding message is expected.

Keep `trial_root` for the next tutorial. Its value is not automatically preserved after the shell closes. Do not replace it with `/` or a system directory. Do not use `sudo` to force a failing trial to succeed.

## 6. Configuration and paths

Loading precedence, from lowest to highest: defaults, system configuration, user configuration, explicit configuration file, command-line overrides.

| Source | Path or selection |
|---|---|
| System configuration | `/etc/athena/athena.toml` |
| User configuration with XDG | `$XDG_CONFIG_HOME/athena/config.toml` |
| Alternative user configuration | `$HOME/.config/athena/config.toml` |
| Explicit file | `--config /path/config.toml` |
| Explicit root | `--root /absolute/path` |
| Historical default root | `/var/lib/athena` |

Always use an explicit root for trials. A minimal configuration file is:

```toml
schema_version = 1
[paths]
root = "/tmp/monad-dedicated-trial"
```

Replace the path with your chosen location. Validate with:

```bash
./build-manual/mpkg --config /absolute/path/config.toml config check
```

An explicit file does not prevent earlier system and user configuration loading; an error in either can still block the trial. Configured paths must be absolute. `config check` validates configuration and reports value provenance. It does not prove that installation will succeed or that the filesystem meets scanner requirements.

With only `--root R`, derived paths are:

| Content | Path |
|---|---|
| Store | `R/store` |
| General state | `R/state` |
| Cache | `R/cache` |
| Build workspace | `R/build` |
| Repository snapshots | `R/repositories/snapshots` |
| Repository state | `R/repositories/state` |
| Catalog manifests | `R/repositories/state/packages/NAME/package.toml` |
| Generations | `R/generations` |
| Activation target | `R/target` |

Configuration also permits specific path overrides. Use `paths` to inspect the effective layout. The current CLI recalculates derived paths when `--root` is supplied; do not assume that directory overrides previously loaded from a file remain effective.

## 7. Your first local package

This example prepares a small, readable source package that installs a text file. It follows the integration-test structure but was not executed during documentation preparation. Scanner requirements in chapter 11 may block installation.

Prerequisites: complete the build and define `trial_root` as in chapter 5. Run from the checkout root in the same shell. Preparation requires Bash, tar, and sha256sum.

```bash
example_root="$(mktemp -d /tmp/monad-example-XXXXXXXX)"
mkdir -p "$example_root/hello-1"
cat > "$example_root/hello-1/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(monad_hello NONE)
install(FILES hello.txt DESTINATION share/monad-hello)
CMAKE
printf '%s\n' 'Hello from Monad!' > "$example_root/hello-1/hello.txt"
tar -czf "$example_root/hello-1.tar.gz" -C "$example_root" hello-1
archive_digest="$(sha256sum "$example_root/hello-1.tar.gz")"
archive_digest="${archive_digest%% *}"
mkdir -p "$trial_root/repositories/state/packages/hello"
cat > "$trial_root/repositories/state/packages/hello/package.toml" <<TOML
name = "hello"
version = "1"
source = "file://$example_root/hello-1.tar.gz"
build_system = "cmake"
sha256 = "$archive_digest"
dependencies = []
TOML
```

The backend installs with prefix `/usr`, so the resulting file appears under `target/usr/share/monad-hello`. Prefixing the absolute path with `file://` produces `file:///tmp/...`. The temporary path is simple, without spaces or shell-sensitive characters. The archive has one top-level directory. `build_system` is explicit because the historical default backend may be Autotools, which the current realization path does not support.

The resulting manifest has this structure:

```toml
name = "hello"
version = "1"
source = "file:///tmp/EXAMPLE-PATH/hello-1.tar.gz"
build_system = "cmake"
sha256 = "ARCHIVE-SHA256-DIGEST"
dependencies = []
```

This last block is illustrative: replace its placeholders. The preceding Bash block fills them automatically.

Start installation and stop if it returns an error:

```bash
./build-manual/mpkg --root "$trial_root" install hello
```

Only after success, inspect the result:

```bash
./build-manual/mpkg --root "$trial_root" list
./build-manual/mpkg --root "$trial_root" info hello
./build-manual/mpkg --root "$trial_root" generation current
cat "$trial_root/target/usr/share/monad-hello/hello.txt"
```

The last file should contain `Hello from Monad!`. For modern memberships, `info` exposes the name, version, repository, and artifact identity. Remove the package only within the trial root:

```bash
./build-manual/mpkg --root "$trial_root" remove hello
./build-manual/mpkg --root "$trial_root" list
./build-manual/mpkg --root "$trial_root" generation list
```

Removal changes the active environment but does not immediately delete artifacts retained by historical generations. Keep trial directories while they are useful for understanding the outcome; this tutorial does not perform automatic cleanup.

For a simple dependency, a manifest can use `dependencies = ["dependency-name"]` with the corresponding package in the local catalog. The internal model supports richer expressions; consult parser sources and tests for the full grammar before writing complex recipes.

## 8. Command reference

Here, `mpkg` means the compiled or installed binary. Always add `--root "$trial_root"` for trials. Uppercase names are placeholders.

| Syntax | Effect |
|---|---|
| `mpkg --help` or `mpkg -h` | General help |
| `mpkg --version` or `mpkg -V` | Version information |
| `mpkg --root R config check` | Validate configuration |
| `mpkg --root R paths` | Show effective paths; diagnostic command |
| `mpkg --root R install NAME` | Resolve and install one requested package name and selected dependencies |
| `mpkg --root R remove NAME [OTHER...]` | Remove one or more packages if the resulting environment is semantically valid |
| `mpkg --root R list` | List packages in the current generation |
| `mpkg --root R info NAME` | Inspect an installed package |
| `mpkg --root R generation list` | List retained generations and mark the current one |
| `mpkg --root R generation current` | Show current generation and memberships |
| `mpkg --root R generation switch ID` | Activate the specified retained generation |
| `mpkg --root R generation rollback` | Select the retained generation with the greatest ID below the current ID |

Global options `--config PATH` and `--root PATH` require values. Use `mpkg --help` for general help; the parser does not provide a uniform contextual-help system for every subcommand. Usage messages that still say `athena` are remnants of the rename.

Public commands for `update`, `upgrade`, `search`, `gc`, remote repository management, or `--dry-run` are not documented here. Diagnostic and test commands exist in the source but are not the ordinary usage path.

## 9. Generations and rollback

A successful installation or removal can create and activate a generation. Retained generations preserve references to required objects. Inspect `generation list` and `generation current` before a transition.

```bash
mpkg --root "$trial_root" generation list
mpkg --root "$trial_root" generation current
# Replace 1 with an appropriate ID that is actually retained:
mpkg --root "$trial_root" generation switch 1
```

`rollback` follows numeric ordering rather than activation history. If retained IDs are 0, 2, and 5 and the current ID is 5, it selects 2. If the current ID is 2, it selects 0 even if 5 was previously active. It reports an error when no lower retained ID exists.

Switching verifies modern objects and validates the relevant semantic state before changing the target. A missing or corrupt object can prevent a transition even when generation metadata exists.

Rollback covers package membership and its projection into the configured target. It is not a complete backup, does not restore personal documents or application databases, and does not promise an atomic restoration of the entire filesystem after power loss. Supporting directories can remain after deactivation.

Generation IDs are neither content digests nor permanent globally unique identifiers: allocation depends on retained generations. Reports should also record source revision and artifact identities.

## 10. Integrity and execution model

The modern pipeline is: local catalog → resolution → plan → local acquisition → optional source-digest verification → private extraction → CMake → staging → output scanning → store publication → generation recording and activation.

A matching digest establishes content correspondence, not author identity or trustworthiness. The system does not automatically provide signatures, producer attestations, or a complete build-provenance chain.

Source-controlled CMake programs run with the current user's privileges. A temporary state root does not restrict their access to other user files or the network. Use trusted sources and a dedicated trial environment. Do not run builds as root.

Current extraction rejects unsupported cases including traversal, duplicate paths, links, and special archive entries. The installed artifact schema may support different features from the restricted source-archive subset.

If a later realization fails, already-published objects may remain in the store without a newly active generation. Do not delete them manually. Retention protections, pins, and garbage collection belong to the internal protocol.

Offline installed-state validation uses persistent metadata to check relevant dependencies, conflicts, and requirements. It does not guarantee that every installed program will execute or interoperate in the actual system. Historical states with incomplete information follow conservative rules: missing data is not treated as proof that dependencies are absent.

## 11. Troubleshooting

| Problem | Check or action |
|---|---|
| CMake not found | Install the development toolchain; the binary cannot be built without it |
| OpenSSL or LibArchive headers missing | Check development packages as well as runtime libraries |
| Core library missing after installation | Preserve relative bin/lib layout and check runtime requirements; see `packaging/INSTALL.md` |
| Invalid configuration | Use absolute paths and `config check`; inspect configuration precedence |
| Missing catalog | Place manifests at `repositories/state/packages/NAME/package.toml` under the effective root |
| Unsupported recipe | Use one local tar.gz source, CMake, and no extra recipe build parameters |
| Source digest mismatch | Inspect archive and manifest; do not replace the digest without understanding the changed bytes |
| Removal rejected | Check resulting dependencies and conflicts; do not edit records manually |
| Switch rejected | Check object presence/integrity, target semantics, and path conflicts |
| No previous generation | Inspect retained IDs; rollback requires a lower ID than the current one |
| Messages mentioning Athena | Check the version; some messages and paths retain the historical name |

### Scanner limitations and `trusted.*` attributes

Some verification must be able to rule out hidden `trusted.*` extended attributes. On filesystems and kernels that prevent ordinary users from performing this inspection, operations may fail with a diagnostic containing `hidden trusted.*`. Some tests return 77 to indicate SKIP in these circumstances.

This is not necessarily a recipe error. The project requires a separate campaign on a suitable environment. Relevant coverage may need `CAP_SYS_ADMIN` in the initial user namespace, or a supported filesystem that explicitly reports xattrs as unsupported. A private user namespace does not automatically meet this requirement.

This manual does not prescribe granting capabilities to the binary, using sudo, or bypassing the scanner. Preserve the diagnostic and observed environment and follow project qualification procedures. Even a correctly prepared example can therefore stop before successful installation.

### Reporting a problem

Include Monad version, checkout revision if available, distribution/kernel/filesystem, exact command, full diagnostics, path configuration, and relevant test results. Provide a minimal reproducer. Remove credentials and personal information from shared logs; retain a private original copy of necessary evidence.

## 12. Contributor guide

### Finding your way through the code

| Path | Responsibility |
|---|---|
| `include/athena/core/` | Public model types and interfaces |
| `src/cli/` | Command parsing and dispatch |
| `src/core/local_repository.cpp` | Local catalog adapter for the public model |
| `src/package/`, `src/dependency/` | Metadata loading and dependency grammar |
| `src/realization/` | Persistent records and realization execution |
| `src/artifact/` | Artifact scanning, schema, and identity |
| `src/store/` | ObjectStore and store protocols |
| `src/install/`, `src/installed/` | Transactions and installed semantic validation |
| `src/generation/` | Membership, persistence, and generation transitions |
| `tests/` | Tests, fixtures, and regressions |
| `docs/adr/` | Rationale for architectural decisions |
| `docs/qualification/`, `qualification/` | Qualification procedures and evidence |

Tracing a request from `src/cli/cli.cpp` is a useful starting point. Read relevant module READMEs and tests first to distinguish public contracts, historical compatibility, and internal tooling.

### Preparing a change

In a real Git checkout, read `AGENTS.md`, then record state, branch, and commit:

```bash
git status --short --branch
git branch --show-current
git rev-parse HEAD
```

The archives used for this edition do not necessarily contain Git history. Do not present an archive hash as a repository commit. Preserve existing changes, untracked files, and evidence. Use isolated builds and keep changes scoped.

Ordinary changes must not modify installed administrative paths, sudoers, gateways, or security pins. These operations follow the project's explicit review and authorization process. Do not implicitly include them in a documentation or application-code fix.

### Validating a change

Build relevant targets and choose tests that exercise the changed behavior. List registered cases with:

```bash
ctest --test-dir build-manual -N
```

After building the corresponding targets, run installation CLI cases with:

```bash
ctest --test-dir build-manual --output-on-failure -LE privileged -R '^cli_install.*test$'
```

Record commands, Debug/Release configuration, separate outcomes, and environment limits. Do not claim completion of a privileged campaign based only on non-privileged tests. Alpha M1 qualification requires the approved Debian environment and the prescribed evidence review.

### Contributing documentation

For each command, document syntax, prerequisites, state changes, an example, and error handling. For each feature, distinguish goals, internal APIs, CLI paths, and qualified behavior. Update the manual alongside related changes and record the verified revision.

Useful contributions include correcting remaining Athena messages, verified examples, error coverage, integration tests, contract clarification, and index maintenance. Architectural extensions require reviewing existing ADRs and module boundaries first.

## 13. Technical documentation map

Paths below are relative to the repository root. This manual belongs at `docs/manual-en.md`; its contents also work as a standalone document.

| Document | Purpose |
|---|---|
| `README.md` | Introduction and declared status |
| `packaging/INSTALL.md` | Installed layout, RUNPATH, staging, and packaging |
| `docs/adr/README.md` | Architecture decision index |
| `docs/adr/0001-build-input-artifact-identity-content-addressed-store.md` | Build-input versus artifact identity |
| `docs/artifact-schema-v1.md` | Artifact schema |
| `docs/m2.2-b1-public-artifact-contract.md` | Relevant public artifact contract |
| `src/install/README.md` | Installation transaction |
| `src/realization/README.md` | RealizationRecord and executor |
| `src/installed/README.md` | Offline validation and historical compatibility |
| `src/generation/README.md` | Generation formats and transitions |
| `src/store/README.md` | Store and its boundaries |
| `docs/alpha-m1-legacy-layout.md` | Historical Alpha M1 layout |
| `docs/security/authorization-policy.md` | Rules for sensitive operations |
| `docs/development/autonomous-infrastructure.md` | Autonomous development infrastructure |
| `docs/qualification/alpha-m1-recovery.md` | Specific Alpha M1 recovery procedures |

Technical documents may describe individual historical phases. Read their context: a component guarantee must not automatically be extended to the complete transaction. Qualification reports describe particular revisions and environments rather than permanent certification.

## 14. Sources and editorial verification

This edition translates the Italian manual. Its technical basis is static inspection of the main README, `CMakeLists.txt`, packaging module, `packaging/INSTALL.md`, CLI, configuration, PathLayout, local adapter, installation/realization/generation/installed-state READMEs, and CLI test fixtures. The relationship with Henosix comes from project context supplied by its author.

Command names, options, derived layout, configuration precedence, minimal recipe structure, numeric rollback selection, and executor limitations were checked in the source. Builds, installation, CTest, and the Alpha M1 campaign were not executed during preparation. The configuration attempt stopped because `cmake` was unavailable. Translating and checking shell syntax does not add runtime qualification.

Supplied archive identities:

| Archive | SHA-256 |
|---|---|
| `monad-source-review.tar.gz` | `95f446cf5aaa0e78288afe6e1598faef56ff572a4946e5c99ae3f80550705872` |
| `monad-review-extra.tar.gz` | `2eb01d8b05f4802108d320bbb11f985213f57116fe5cfa7b38d090750c866e9b` |

These hashes identify documentation inputs, not an approved release. For future publication, associate the manual with the actual source commit, rerun examples on a suitable machine, and update verification notes with observed results and limits.

Monad Package Manager: copyright © 2026 Alessandro Bifaretti. The declared software license is GPL-3.0-or-later; see the project's `LICENSE` and `COPYRIGHT` files.
