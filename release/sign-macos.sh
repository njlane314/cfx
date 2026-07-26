#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 BINARY" >&2
    exit 2
fi

binary=$1
[[ -x $binary ]] || { echo "macOS signing: not executable: $binary" >&2; exit 1; }

required=(
    APPLE_CERTIFICATE_P12_BASE64
    APPLE_CERTIFICATE_PASSWORD
    APPLE_SIGNING_IDENTITY
    APPLE_NOTARY_APPLE_ID
    APPLE_NOTARY_TEAM_ID
    APPLE_NOTARY_PASSWORD
)
for name in "${required[@]}"; do
    [[ -n ${!name:-} ]] || { echo "macOS signing: $name is required" >&2; exit 1; }
done

work=$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/cfx-sign.XXXXXX")
certificate=$work/developer-id.p12
keychain=$work/signing.keychain-db
notary_archive=$work/cfx-notary.zip
keychain_password=$(openssl rand -hex 24)
trap 'security delete-keychain "$keychain" >/dev/null 2>&1 || true; rm -rf "$work"' EXIT

printf '%s' "$APPLE_CERTIFICATE_P12_BASE64" |
    openssl base64 -d -A >"$certificate"
security create-keychain -p "$keychain_password" "$keychain"
security set-keychain-settings -lut 21600 "$keychain"
security unlock-keychain -p "$keychain_password" "$keychain"
security import "$certificate" -k "$keychain" -P "$APPLE_CERTIFICATE_PASSWORD" \
    -T /usr/bin/codesign
security set-key-partition-list -S apple-tool:,apple: -s \
    -k "$keychain_password" "$keychain" >/dev/null

codesign --force --options runtime --timestamp --keychain "$keychain" \
    --sign "$APPLE_SIGNING_IDENTITY" "$binary"
codesign --verify --strict --verbose=2 "$binary"

ditto -c -k --keepParent "$binary" "$notary_archive"
xcrun notarytool submit "$notary_archive" \
    --apple-id "$APPLE_NOTARY_APPLE_ID" \
    --team-id "$APPLE_NOTARY_TEAM_ID" \
    --password "$APPLE_NOTARY_PASSWORD" \
    --wait
