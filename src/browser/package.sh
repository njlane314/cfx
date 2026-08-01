#!/usr/bin/env bash

set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
manifest=$script_dir/manifest.json
extension_id_file=$script_dir/extension-id

if ! command -v zip >/dev/null 2>&1; then
    echo "browser package: zip is required" >&2
    exit 1
fi
if ! command -v node >/dev/null 2>&1; then
    echo "browser package: node is required to verify the extension identity" >&2
    exit 1
fi

version=$(
    sed -n 's/^[[:space:]]*"version":[[:space:]]*"\([^"]*\)".*/\1/p' "$manifest"
)
if [[ ! $version =~ ^[0-9]+([.][0-9]+){0,3}$ ]]; then
    echo "browser package: cannot read a valid manifest version" >&2
    exit 1
fi

manifest_key=$(
    sed -n 's/^[[:space:]]*"key":[[:space:]]*"\([^"]*\)".*/\1/p' "$manifest"
)
if [[ ! -f $extension_id_file ]]; then
    echo "browser package: missing src/browser/extension-id" >&2
    exit 1
fi
extension_id=$(tr -d '[:space:]' < "$extension_id_file")
if [[ -z $manifest_key ]]; then
    echo "browser package: manifest.json has no public key" >&2
    exit 1
fi
if [[ ! $extension_id =~ ^[a-p]{32}$ ]]; then
    echo "browser package: src/browser/extension-id is not a Chrome extension ID" >&2
    exit 1
fi

derive_id_with_node() {
    node - "$manifest" <<'NODE'
const crypto = require("node:crypto");
const fs = require("node:fs");

const manifest = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));
const digest = crypto.createHash("sha256")
    .update(Buffer.from(manifest.key, "base64"))
    .digest("hex")
    .slice(0, 32);
const id = digest.replace(/[0-9a-f]/g, (digit) =>
    String.fromCharCode("a".charCodeAt(0) + Number.parseInt(digit, 16)));
process.stdout.write(id + "\n");
NODE
}

if ! derived_id=$(derive_id_with_node); then
    echo "browser package: cannot decode the manifest public key" >&2
    exit 1
fi

if [[ $derived_id != "$extension_id" ]]; then
    echo "browser package: manifest key derives $derived_id, not $extension_id" >&2
    exit 1
fi

output_dir=$repo_root/.build/browser
archive=$output_dir/cfx-connector-$version.zip
stage=$(mktemp -d "${TMPDIR:-/tmp}/cfx-browser.XXXXXX")
trap 'rm -rf "$stage"' EXIT

mkdir -p "$output_dir"
cp "$manifest" \
    "$script_dir/background.js" \
    "$script_dir/samples.js" \
    "$script_dir/submission.js" \
    "$script_dir/connector.js" \
    "$stage/"
cp -R "$script_dir/icons" "$stage/"

rm -f "$archive"
(
    cd "$stage"
    zip -q -X -r "$archive" .
)

echo "$archive"
