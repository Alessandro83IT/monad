# Installing and staging Athena

Configure with relative GNUInstallDirs destinations (`CMAKE_INSTALL_BINDIR`,
`CMAKE_INSTALL_LIBDIR`, and `CMAKE_INSTALL_INCLUDEDIR`). Paths are lexically
normalized: `tools/../bin` becomes `bin`, and `a/b/../lib` becomes `a/lib`.
Absolute paths and paths escaping the prefix, such as `../bin` or `a/../../lib`,
are rejected. Normalized destinations are used for installation and RUNPATH.
Custom relative destinations, including `usr/bin`, remain supported; their
components are intentionally appended to the selected prefix.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
DESTDIR=/tmp/athena-stage cmake --install build --prefix /usr
ctest --test-dir build -L packaging --output-on-failure
```

The configure-time prefix selects GNUInstallDirs defaults. The install-time
`--prefix` replaces that prefix but retains the configured relative destinations;
it does not reselect libdir. To choose a distribution's multiarch or lib64 layout,
configure with that prefix or set `CMAKE_INSTALL_LIBDIR` explicitly. DESTDIR is
only the filesystem staging root prepended to the installed prefix; it is never
part of the executable's RUNPATH. Noesix builders can use the same staging command.

On Linux, the executable has DT_RUNPATH `$ORIGIN/<relative path from bindir to
libdir>` (typically `$ORIGIN/../lib` or `$ORIGIN/../lib64`). A staged tree can be
executed directly, and the prefix tree can be moved intact. Its bin/lib relative
relationship must be retained. This is a runtime/header installation, not a
CMake `find_package(Athena)` export.

Core's installed RUNPATH is empty by default and system dependencies use normal
loader lookup. CMake's explicit `CMAKE_INSTALL_RPATH` setting initializes core's
INSTALL_RPATH and is preserved. For dependencies deployed in `<prefix>/vendor/lib`
with core in `<prefix>/lib` or `<prefix>/lib64`, a packager can use:

```sh
cmake -S . -B build -DCMAKE_INSTALL_RPATH='$ORIGIN/../vendor/lib'
```

Packagers must choose relocatable entries appropriate to their configured libdir
and deploy the dependencies themselves. Explicit settings follow normal CMake
semantics, including lists and generator expressions; Athena does not validate
or rewrite core's explicitly supplied INSTALL_RPATH. Athena's executable lookup
to core remains independently derived. RUNPATH is not transitive: dependencies
of core need core's own lookup, and dependencies of deployed libraries need their
own loader strategy. Future Athena libraries installed beside core would require
core's own `$ORIGIN` RUNPATH.

Link-time discovery and normal build-tree RPATH still support libraries found in
nonstandard locations. Installation never automatically adds their absolute
directories, even if `CMAKE_INSTALL_RPATH_USE_LINK_PATH` was enabled globally.
Explicit packager choices are separate from automatic discovery. CMake's options
that deliberately disable runtime paths can prevent staged execution; the smoke
test reports that failure rather than supplying a loader environment workaround.

AppImage packaging specifically requires the effective normalized bindir to be
`bin`. The script checks this before deleting an existing AppDir; `tools/bin`,
`sbin`, and other custom bindirs remain supported for ordinary installation.
It stages with DESTDIR set to AppDir and prefix `/usr`, preserving AppRun's
`usr/bin/athena` layout. Core can use lib, lib64, or a nested libdir because its
lookup is relative. External dependencies are not bundled by this change.
`ATHENA_APPIMAGE_BUILD_DIR` selects the configured build (the custom target sets
it automatically); `ATHENA_APPIMAGE_DIST_DIR` optionally selects the output
folder. Defaults remain the project's `build` and `dist` directories.
`ATHENA_APPIMAGE_CONFIG` selects a configuration for multi-config builds; the
custom target supplies its active configuration.

The Linux packaging tests require `readelf`, `ldd`, and `mktemp`. Each invocation
atomically allocates a unique staging directory containing spaces. Separate
subtrees test `/usr` and `/opt/athena`; the whole installed inventory of files,
directories, and symlinks must match the configured destinations. ELF inspection uses `LC_ALL=C`, and runtime
checks unset `LD_LIBRARY_PATH` and `LD_PRELOAD`. Successful trees are removed;
failures retain artifacts for diagnosis. The regression test exercises the same
production packaging module through small linked fixture targets, covering
custom layouts, normalization, explicit core RUNPATH, nonstandard discovery,
concurrent test processes, and the real AppImage script with an appimagetool stub.
The main smoke test independently installs and executes the real Athena binary.
