# Monad Package Manager

Monad Package Manager is an experimental package manager for Linux.

The project aims to combine:

- source-based package building inspired by Gentoo/Portage;
- dependency resolution;
- declarative system configuration inspired by Nix;
- immutable package storage;
- system generations;
- configuration rollback;
- integration with the Arch Linux ecosystem.

The initial implementation is written in C++20.

## Status

Experimental release candidate — version 0.1.0-alpha.1. Monad is not
production-ready and is not a dependable replacement for pacman, emerge, or
Nix. Run it only with a dedicated, disposable, non-privileged root.

`mpkg install <package>` uses the content-addressed pipeline: the existing
local catalog under `repository_state/packages` supplies public repository
candidates, public resolution produces a core install plan, and the install
transaction realizes artifacts into ObjectStore and activates a new generation.
The legacy installer remains compatibility code but is no longer the CLI install
path. Current realization supports only one local `file:///absolute/path` gzip
tar source and CMake, without build parameters. Network sources, Autotools,
binary import and Athena IR are unsupported. See [install](src/install/README.md)
and [realization](src/realization/README.md) for implementation details.

M2.4 persists installed dependency semantics with realization records and validates
complete resulting environments before install/remove activation. Partial removal
and replacements are checked offline against preserved consumers, capabilities,
features, conflicts and system policies. Historical records remain readable under
an explicit conservative compatibility policy. See [installed semantic state](src/installed/README.md).

## Build

Requirements:

- Linux, C++20 compiler, CMake 3.20 or newer, Ninja or Make
- OpenSSL and LibArchive development files, pthreads
- Python 3 for parser and packaging checks

Build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure -LE privileged
```

Run:

```bash
./build/mpkg --help
./build/mpkg --version
```

## Non-privileged trial

Use a fresh user-owned root for package-manager state:

```bash
trial_root="$(mktemp -d)"
./build/mpkg --root "$trial_root" list
./build/mpkg --root "$trial_root" generation list
rm -rf "$trial_root"
```

Install and remove can create persistent state. Try them only with a disposable
root and a local package fixture. Build scripts run as the current user and
should be treated as untrusted code. Never run Monad as root or point `--root`
at a real system. See [install details](src/install/README.md),
[realization details](src/realization/README.md), and
[installed-state details](src/installed/README.md).

## Project goals

Monad aims to combine source-based package building with dependency resolution,
immutable storage, and package generations. Compatibility claims are limited to
the configurations exercised by this experimental release candidate.

The project is initially developed and tested on Arch Linux.

## License

Monad Package Manager is free software licensed under the
GNU General Public License, version 3 or (at your option) any later version
(GPL-3.0-or-later).

Copyright (C) 2026 Alessandro Bifaretti.

See [LICENSE](LICENSE) for the full license text and [COPYRIGHT](COPYRIGHT)
for copyright information.
