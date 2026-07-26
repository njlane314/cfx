# Releases

A `vX.Y.Z` tag builds four native archives: macOS and Linux on arm64 and
x86_64. It also rebuilds the polished demonstration as
`cfx-X.Y.Z-demo-20s.mp4`. Each native archive has the same small layout:

```text
bin/cfx
libexec/cfx
share/man/man1/cfx.1
share/cfx/include/
share/cfx/templates/
share/cfx/browser/extension-id
LICENSE
```

`bin/cfx` sets `CFX_ASSET_ROOT` to `share/cfx` and executes the native binary.
The writable workspace remains the current project (or `CFX_ROOT` when set).

## Signing and verification

Every archive, the source archive, connector ZIP, demonstration, and checksum
manifest receives GitHub's Sigstore-backed, cryptographically signed
build-provenance attestation. The demonstration is rebuilt on macOS from the
checked-in capture and renderer. A separate `ffprobe` gate requires a duration
of exactly `20.000000` seconds, `1920x1080` dimensions, and exactly one video
stream with no audio. When native Apple signing is enabled, the workflow also
signs macOS executables with a Developer ID Application certificate, enables
the hardened runtime, and submits them to Apple's notary service.

After downloading an archive, verify both its digest and provenance:

```sh
shasum -a 256 -c SHA256SUMS
gh attestation verify cfx-1.2.3-macos-arm64.tar.gz \
  --repo njlane314/cfx
```

On macOS, the native signature can be inspected after extraction:

```sh
codesign --verify --strict --verbose=2 cfx-1.2.3-macos-arm64/libexec/cfx
codesign -dv --verbose=4 cfx-1.2.3-macos-arm64/libexec/cfx
```

## Homebrew

The public formula lives in the conventional `njlane314/homebrew-cfx` tap.
After the first tagged release:

```sh
brew install njlane314/cfx/cfx
```

The tag workflow renders the formula with the release source checksum, runs its
style and installation tests on macOS, then advances `Formula/cfx.rb` in that
tap. The copy in this repository is the head-development formula; maintainers
can test it with `brew install --HEAD ./Formula/cfx.rb`.

## Maintainer release

1. Confirm `make verify` and `bash scripts/release/test.sh` pass on `main`.
2. Confirm the Chrome Web Store release is ready and its manifest version is
   final.
3. Create and push an annotated semantic-version tag, for example
   `git tag -s v1.0.0 && git push origin v1.0.0`.
4. Confirm all four native builds and the independently probed demo job pass,
   then confirm the GitHub Release, its attestations, and the formula
   publication job succeed.
5. Enable immutable releases in the repository settings, then verify with
   `gh release verify v1.0.0`.

Native Apple signing is optional because every artifact always has signed
GitHub provenance. Enable it with the repository variable
`APPLE_CODESIGN_ENABLED=true`; its macOS jobs then require these secrets:

- `APPLE_CERTIFICATE_P12_BASE64`: base64-encoded Developer ID Application
  certificate and private key in PKCS#12 format.
- `APPLE_CERTIFICATE_PASSWORD`: password for that PKCS#12 file.
- `APPLE_SIGNING_IDENTITY`: exact Developer ID Application identity.
- `APPLE_NOTARY_APPLE_ID`, `APPLE_NOTARY_TEAM_ID`, and
  `APPLE_NOTARY_PASSWORD`: Apple notary-service credentials; the password is an
  app-specific password.

Formula publication requires a public `njlane314/homebrew-cfx` repository,
the repository variable `HOMEBREW_TAP_ENABLED=true`, and
`HOMEBREW_TAP_TOKEN`, a fine-grained token with Contents read/write permission
for that repository. Until configured, formula publication is skipped without
blocking signed release provenance. GitHub supplies the cfx release token and
OIDC identity. No long-lived Linux signing key is needed.
