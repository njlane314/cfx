#!/usr/bin/env bash

set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
output_dir=${1:-$repo_root/.build/browser/store}
capture=$output_dir/capture.txt

if [[ $(uname -s) != Darwin ]] || ! command -v swiftc >/dev/null 2>&1 ||
    ! command -v node >/dev/null 2>&1; then
    echo 'browser assets: macOS, swiftc, and node are required' >&2
    exit 1
fi

build_dir=$(mktemp -d "${TMPDIR:-/tmp}/cfx-demo.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT

"$script_dir/capture.sh" "$capture" >/dev/null
swiftc -parse-as-library -O "$script_dir/render.swift" -o "$build_dir/render"
"$build_dir/render" --assets "$capture" "$output_dir"
node "$script_dir/../icon.js" "$output_dir" 128 >/dev/null
"$build_dir/render" "$capture" "$output_dir/demo-20s.mp4"
"$build_dir/render" --verify "$output_dir"

printf '%s\n' \
    "$output_dir/icon-128.png" \
    "$output_dir/screenshot-1280x800.png" \
    "$output_dir/promo-small-440x280.png" \
    "$output_dir/promo-marquee-1400x560.png" \
    "$output_dir/demo-20s.mp4"
