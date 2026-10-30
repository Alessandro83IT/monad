#!/bin/sh
# Validate the AppDir passed by the real script without requiring appimagetool.
set -eu
test -x "$1/usr/bin/mpkg"
test ! -e "$1/usr/usr/bin/mpkg"
env -u LD_LIBRARY_PATH -u LD_PRELOAD "$1/AppRun" --version
env -u LD_LIBRARY_PATH -u LD_PRELOAD "$1/AppRun" --help
