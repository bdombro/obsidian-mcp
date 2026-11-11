#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NAME="obsidian-mcp"
BIN_PATH="$ROOT/build/bin/$NAME"
STAGING_DIR="$ROOT/Formula/.staging"
STAGING_BIN="$STAGING_DIR/$NAME"
FORMULA_PATH="$ROOT/Formula/$NAME.rb"
BACKUP_PATH="$STAGING_DIR/$NAME.rb.bak"

usage() {
  echo "Usage: $0 <install|reset>" >&2
  exit 1
}

cmd="${1:-}"
case "$cmd" in
  install)
    if [[ ! -x "$BIN_PATH" ]]; then
      echo "Missing binary: $BIN_PATH (run just build first)" >&2
      exit 1
    fi
    if [[ ! -f "$FORMULA_PATH" ]]; then
      echo "Missing formula: $FORMULA_PATH" >&2
      exit 1
    fi
    mkdir -p "$STAGING_DIR"
    if [[ ! -f "$BACKUP_PATH" ]]; then
      cp "$FORMULA_PATH" "$BACKUP_PATH"
    fi
    cp "$BIN_PATH" "$STAGING_BIN"
    chmod 0755 "$STAGING_BIN"
    if command -v shasum >/dev/null 2>&1; then
      sha="$(shasum -a 256 "$STAGING_BIN" | awk '{print $1}')"
    else
      sha="$(sha256sum "$STAGING_BIN" | awk '{print $1}')"
    fi
    awk -v url="  url \"file://$STAGING_BIN\"" -v sha="  sha256 \"$sha\"" '
      /^[[:space:]]*url "/ { print url; next }
      /^[[:space:]]*sha256 "/ { print sha; next }
      { print }
    ' "$BACKUP_PATH" > "$FORMULA_PATH.tmp"
    mv "$FORMULA_PATH.tmp" "$FORMULA_PATH"
    echo "Staged dev formula: $FORMULA_PATH"
    ;;
  reset)
    if [[ -f "$BACKUP_PATH" ]]; then
      cp "$BACKUP_PATH" "$FORMULA_PATH"
      echo "Restored $FORMULA_PATH"
    fi
    rm -rf "$STAGING_DIR"
    ;;
  *)
    usage
    ;;
esac
