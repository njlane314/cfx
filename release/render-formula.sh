#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 --head | $0 VERSION SOURCE_SHA256" >&2
    exit 2
}

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
formula=$script_dir/cfx.rb
anchor='  homepage "https://github.com/njlane314/cfx"'

if [[ $# -eq 1 && $1 == --head ]]; then
    release=
elif [[ $# -eq 2 && $1 =~ ^[0-9]+([.][0-9]+){2}(-[0-9A-Za-z][0-9A-Za-z.-]*)?$ && $2 =~ ^[0-9a-f]{64}$ ]]; then
    version=$1
    sha256=$2
    release=$(printf '  url "https://github.com/njlane314/cfx/releases/download/v%s/cfx-%s-source.tar.gz"\n  sha256 "%s"' \
        "$version" "$version" "$sha256")
else
    usage
fi

anchor_count=$(grep -Fxc "$anchor" "$formula" || true)
[[ $anchor_count == 1 ]] || { echo "formula render: expected one homepage anchor, found $anchor_count" >&2; exit 1; }

while IFS= read -r line; do
    printf '%s\n' "$line"
    if [[ $line == "$anchor" ]]; then
        [[ -z $release ]] || printf '%s\n' "$release"
    fi
done <"$formula"
