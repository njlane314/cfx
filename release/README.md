# Releases

## PUBLISH

Release from a clean `main` checkout:

```sh
version=1.0.0
make verify
bash release/test.sh
git tag -s "v$version"
git push origin "v$version"
```

Before tagging, match `src/browser/manifest.json` to the tag and complete the
Chrome Web Store checklist in `src/browser/STORE.md`.

Confirm the native, metadata, publication, and optional Homebrew jobs.
Enable immutable releases in the repository settings, then run:

```sh
gh release verify v1.0.0
```

## OUTPUT

A `vX.Y.Z` tag produces:

```text
cfx-X.Y.Z-{macos,linux}-{arm64,x86_64}.tar.gz
cfx-X.Y.Z-source.tar.gz
cfx-connector-X.Y.Z.zip
SHA256SUMS
```

Native archives contain:

```text
bin/cfx
libexec/cfx
share/man/man1/cfx.1
share/cfx/include/
share/cfx/templates/
share/cfx/browser/extension-id
LICENSE
```

`bin/cfx` sets `CFX_ASSET_ROOT` and runs `libexec/cfx`. Solutions and runtime
state remain outside the installation.

## VERIFY

From a download directory within the checkout:

```sh
shasum -a 256 -c SHA256SUMS
repo=$(gh repo view --json nameWithOwner -q .nameWithOwner)
gh attestation verify cfx-1.2.3-macos-arm64.tar.gz --repo "$repo"
```

Every artifact has GitHub build provenance.

For an Apple-signed archive:

```sh
codesign --verify --strict --verbose=2 \
  cfx-1.2.3-macos-arm64/libexec/cfx
codesign -dv --verbose=4 cfx-1.2.3-macos-arm64/libexec/cfx
```

## SIGNING

Apple signing and notarisation are optional:

```text
APPLE_CODESIGN_ENABLED=true
APPLE_CERTIFICATE_P12_BASE64
APPLE_CERTIFICATE_PASSWORD
APPLE_SIGNING_IDENTITY
APPLE_NOTARY_APPLE_ID
APPLE_NOTARY_TEAM_ID
APPLE_NOTARY_PASSWORD
```

The certificate is a PKCS#12 Developer ID Application bundle;
`APPLE_NOTARY_PASSWORD` is an app-specific password. GitHub provenance needs
no long-lived Linux signing key.

## HOMEBREW

Test the development formula:

```sh
brew install --HEAD ./release/cfx.rb
```

Tagged publication requires:

```text
HOMEBREW_TAP_ENABLED=true
HOMEBREW_TAP_TOKEN
```

The token needs Contents read/write permission for the public tap. The workflow
binds the formula to the source checksum, tests it on macOS, and updates
`Formula/cfx.rb`. Disabled formula publication does not block the release.
