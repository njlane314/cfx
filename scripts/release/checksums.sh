#!/usr/bin/env bash
set -euo pipefail

if [[ $# -eq 0 ]]; then
    echo "usage: $0 FILE..." >&2
    exit 2
fi

if command -v sha256sum >/dev/null 2>&1; then
    digest() { sha256sum "$1" | cut -d ' ' -f 1; }
elif command -v shasum >/dev/null 2>&1; then
    digest() { shasum -a 256 "$1" | cut -d ' ' -f 1; }
else
    echo "release checksums: sha256sum or shasum is required" >&2
    exit 1
fi

for file in "$@"; do
    [[ -f $file ]] || { echo "release checksums: not a file: $file" >&2; exit 1; }
    printf '%s  %s\n' "$(digest "$file")" "$(basename "$file")"
done | LC_ALL=C sort -k2
