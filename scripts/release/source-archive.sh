#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 || ! $1 =~ ^[0-9]+([.][0-9]+){2}(-[0-9A-Za-z][0-9A-Za-z.-]*)?$ ]]; then
    echo "usage: $0 VERSION [OUTPUT_DIR]" >&2
    exit 2
fi

version=$1
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
output_dir=${2:-$root/.build/release}
archive=$output_dir/cfx-$version-source.tar.gz

mkdir -p "$output_dir"
rm -f "$archive" "$archive.tmp"
git -C "$root" archive --format=tar --prefix="cfx-$version/" HEAD |
    gzip -n >"$archive.tmp"
mv "$archive.tmp" "$archive"

printf '%s\n' "$archive"
