#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 VERSION {macos|linux} {arm64|x86_64} [BINARY] [OUTPUT_DIR]" >&2
    exit 2
}

[[ $# -ge 3 && $# -le 5 ]] || usage

version=$1
platform=$2
architecture=$3
[[ $version =~ ^[0-9]+([.][0-9]+){2}(-[0-9A-Za-z][0-9A-Za-z.-]*)?$ ]] || usage
[[ $platform == macos || $platform == linux ]] || usage
[[ $architecture == arm64 || $architecture == x86_64 ]] || usage

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
binary=${4:-$root/.build/tools/cfx}
output_dir=${5:-$root/.build/release}

[[ -x $binary ]] || { echo "release package: not executable: $binary" >&2; exit 1; }
for path in LICENSE templates/solution.cpp include/cp browser/extension-id; do
    [[ -e $root/$path ]] || { echo "release package: missing $path" >&2; exit 1; }
done

name=cfx-$version-$platform-$architecture
stage_root=$output_dir/stage
stage=$stage_root/$name
archive=$output_dir/$name.tar.gz

rm -rf "$stage"
mkdir -p "$stage/bin" "$stage/libexec" "$stage/share/cfx/browser"
install -m 0755 "$script_dir/cfx-wrapper.sh" "$stage/bin/cfx"
install -m 0755 "$binary" "$stage/libexec/cfx"
cp -R "$root/templates" "$stage/share/cfx/"
cp -R "$root/include" "$stage/share/cfx/"
install -m 0644 "$root/browser/extension-id" "$stage/share/cfx/browser/extension-id"
install -m 0644 "$root/LICENSE" "$stage/LICENSE"

mkdir -p "$output_dir"
rm -f "$archive" "$archive.tmp"
(
    cd "$stage_root"
    COPYFILE_DISABLE=1 tar -cf - "$name"
) | gzip -n >"$archive.tmp"
mv "$archive.tmp" "$archive"

printf '%s\n' "$archive"
