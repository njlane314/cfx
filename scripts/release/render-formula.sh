#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 --head | $0 VERSION SOURCE_SHA256" >&2
    exit 2
}

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
template=$script_dir/cfx.rb.in

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

while IFS= read -r line; do
    if [[ $line == @RELEASE@ ]]; then
        [[ -z $release ]] || printf '%s\n' "$release"
    else
        printf '%s\n' "$line"
    fi
done <"$template"
