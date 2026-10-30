#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
if ! command -v codex >/dev/null 2>&1; then
    printf 'codex CLI is not installed or not on PATH. Install and verify it before starting a session.\n' >&2
    exit 127
fi

cd -- "$repo_root"
exec codex "$@"
