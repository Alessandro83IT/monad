# Athena Package Manager

Athena Package Manager is an experimental package manager for Linux.

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

Early development — version 0.1.0.

`athena install <package>` uses the content-addressed pipeline: the existing
local catalog under `repository_state/packages` supplies public repository
candidates, public resolution produces a core install plan, and the install
transaction realizes artifacts into ObjectStore and activates a new generation.
The legacy installer remains compatibility code but is no longer the CLI install
path. Current realization supports only one local `file:///absolute/path` gzip
tar source and CMake, without build parameters; remote sources, Autotools,
binary import and Athena IR are unsupported. See [install](src/install/README.md)
and [realization](src/realization/README.md) for the precise limitations.

At this stage the project contains the initial CMake-based C++ application.

## Build

Requirements:

- C++20 compiler
- CMake 3.20 or newer

Build:

```bash
cmake -S . -B build
cmake --build build

```

Run:

```bash
./build/athena
```

## Project goals

Athena aims to provide a package management system that combines the flexibility of source-based package management with the reproducibility and declarative model of functional package managers.

The project is initially developed and tested on Arch Linux.

## License

License to be decided.
