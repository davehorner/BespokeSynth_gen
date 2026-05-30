#!/usr/bin/env sh
set -eu

if command -v python3 >/dev/null 2>&1; then
  exec python3 scripts/download-rust-models.py "$@"
fi

exec python scripts/download-rust-models.py "$@"
