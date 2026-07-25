#!/usr/bin/env bash

set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
manifest=$script_dir/manifest.json
extension_id_file=$script_dir/extension-id

if ! command -v zip >/dev/null 2>&1; then
    echo "browser package: zip is required" >&2
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
    echo "browser package: missing browser/extension-id" >&2
    exit 1
fi
extension_id=$(tr -d '[:space:]' < "$extension_id_file")
if [[ -z $manifest_key ]]; then
    echo "browser package: manifest.json has no public key" >&2
    exit 1
fi
if [[ ! $extension_id =~ ^[a-p]{32}$ ]]; then
    echo "browser package: browser/extension-id is not a Chrome extension ID" >&2
    exit 1
fi

derive_id_with_openssl() {
    local digest
    digest=$(
        printf '%s' "$manifest_key" |
            openssl base64 -d -A 2>/dev/null |
            openssl dgst -sha256 -binary |
            od -An -tx1 |
            tr -d ' \n' |
            cut -c1-32
    ) || return 1
    [[ ${#digest} -eq 32 ]] || return 1
    printf '%s\n' "$digest" | tr '0123456789abcdef' 'abcdefghijklmnop'
}

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

derived_id=
derivation_tool_found=false
if command -v openssl >/dev/null 2>&1 && command -v od >/dev/null 2>&1; then
    derivation_tool_found=true
    derived_id=$(derive_id_with_openssl) || derived_id=
fi
if [[ -z $derived_id ]] && command -v node >/dev/null 2>&1; then
    derivation_tool_found=true
    if ! derived_id=$(derive_id_with_node); then
        echo "browser package: cannot decode the manifest public key" >&2
        exit 1
    fi
fi
if [[ -z $derived_id ]]; then
    if [[ $derivation_tool_found == true ]]; then
        echo "browser package: cannot decode the manifest public key" >&2
    else
        echo "browser package: openssl (plus od) or node is required to verify the extension ID" >&2
    fi
    exit 1
fi

if [[ $derived_id != "$extension_id" ]]; then
    echo "browser package: manifest key derives $derived_id, not $extension_id" >&2
    exit 1
fi

output_dir=$repo_root/.build/browser
archive=$output_dir/cf-probs-connector-$version.zip
stage=$(mktemp -d "${TMPDIR:-/tmp}/cfprobs-browser.XXXXXX")
trap 'rm -rf "$stage"' EXIT

mkdir -p "$stage/icons" "$output_dir"
cp "$manifest" "$script_dir/background.js" "$script_dir/connector.js" "$stage/"
for size in 16 32 48 128; do
    icon=$script_dir/icons/icon-$size.png
    if [[ ! -f $icon ]]; then
        echo "browser package: missing $icon" >&2
        exit 1
    fi
    cp "$icon" "$stage/icons/"
done

rm -f "$archive"
(
    cd "$stage"
    zip -q -X -r "$archive" .
)

echo "$archive"
