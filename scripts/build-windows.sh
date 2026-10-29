#!/bin/sh
# Delegate to the native script for quoted paths, no Python dependency, and
# incremental compilation with header/flag tracking.
set -eu
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
SCRIPT="$SCRIPT_DIR/build-windows.ps1"
if command -v cygpath >/dev/null 2>&1; then
  SCRIPT="$(cygpath -w "$SCRIPT")"
fi
exec powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$SCRIPT" "$@"
