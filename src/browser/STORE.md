# Chrome Web Store release

Build the upload archive and listing assets from the repository root:

```sh
make browser-package
src/browser/demo/build.sh
```

The resulting `.build/browser/cfx-connector-<version>.zip` has
`manifest.json` at its root and contains only runtime files.
`src/browser/demo/build.sh` runs the real CLI and loopback bridge against isolated
fixtures, then writes the following untracked publication assets:

- `.build/browser/store/icon-128.png`
- `.build/browser/store/screenshot-1280x800.png`
- `.build/browser/store/promo-small-440x280.png`
- `.build/browser/store/promo-marquee-1400x560.png`
- `.build/browser/store/demo-20s.mp4`

Asset rendering requires macOS, Swift, and Node.js. Runtime packaging requires
Node.js and `zip`; neither command downloads a dependency.

No raster or vector artwork is stored in the repository. The build scripts,
dependency-free icon renderer, and Swift listing renderer are the auditable
source of every visual. Packaging injects the required generated icons and
their manifest entries only into the temporary ZIP stage, so loading the
image-free source directory unpacked also remains valid. The screenshot and
video use output captured from an actual isolated `cfx 71A` fetch, compile,
test, bridge, and verdict flow; the fixture prevents a network request or real
submission. The MP4 is silent, 1920 x 1080, and exactly 20 seconds long.

## Release checklist

- [ ] Upload archive built by `make browser-package`.
- [ ] Distribution visibility is **Public**, all intended regions are selected,
  and the category is **Developer Tools**.
- [ ] Store public key and item ID have replaced the development values in
  `src/browser/manifest.json` and `assets/browser/extension-id`.
- [ ] 128 x 128 store icon: `.build/browser/store/icon-128.png`.
- [ ] 1280 x 800 screenshot:
  `.build/browser/store/screenshot-1280x800.png`.
- [ ] 440 x 280 small promo tile:
  `.build/browser/store/promo-small-440x280.png`.
- [ ] 1400 x 560 marquee tile:
  `.build/browser/store/promo-marquee-1400x560.png`.
- [ ] The 20-second MP4 has been uploaded to YouTube and its URL supplied as
  the localized promotional video.
- [ ] Privacy policy URL:
  `https://github.com/njlane314/cfx/blob/main/src/browser/PRIVACY.md`.
- [ ] Website URL: `https://github.com/njlane314/cfx`.
- [ ] Support URL: `https://github.com/njlane314/cfx/issues`.
- [ ] Data-use disclosures and permission justifications match the copy below.
- [ ] Reviewer instructions are included and the final ZIP has been tested with
  **Load unpacked**.

## First draft

1. Register a Chrome Web Store developer account and create a new item.
2. Upload the ZIP and save it as a draft.
3. In **Package**, copy the item ID and **View public key** value.
4. Replace the development `key` in `src/browser/manifest.json` with that
   public key, and replace `assets/browser/extension-id` with the item ID.
5. Rebuild and upload the final ZIP.
6. Paste the copy below, upload the generated artwork, and add the YouTube demo
   URL.
7. In **Privacy**, make the disclosures below and certify Limited Use.
8. In **Distribution**, select **Public** visibility and the intended regions.
9. Submit for review with automatic publishing enabled, or publish the approved
   draft within the dashboard's stated window.

The tracked development key gives **Load unpacked** a stable ID; it has no
private signing key. Do not publish until the Web Store public key and item ID
have replaced both development values and are committed. Public keys and item
IDs are not secrets; signing keys and account credentials must never enter
this repository.

## Ready-to-paste listing

Name:

> cfx — Codeforces workflow connector

Summary:

> The small, auditable, two-command Codeforces workflow: fetch samples, test locally, and submit exact C++ source.

Category:

> Developer Tools

Detailed description:

> The cfx connector is the small, auditable, two-command Codeforces workflow.
> It links explicit commands from the local open-source C++ workbench to
> Codeforces in Chrome.
>
> Run `cfx 71A` to create a workspace and import public problem metadata and
> samples. Write and test the solution locally, then run `cfx submit` to send
> the exact tested C++ bundle through your existing signed-in Codeforces tab.
>
> Before installation: the connector handles public Codeforces page content,
> your Codeforces username, the selected problem, exact source code, and the
> resulting submission identity only to perform those two explicit commands.
> Data is shared only with your local cfx process and Codeforces. It is never
> sent to the cfx developer or another third party.
>
> The extension has no popup and does nothing by itself. It responds only to a
> short-lived, token-protected listener started on `127.0.0.1` by an explicit
> cfx command. It has no analytics, advertising, remote code, persistent
> account storage, or developer-operated server. Codeforces credentials and
> session cookies stay in Chrome.

Single purpose:

> Connect explicit cfx CLI actions to Codeforces pages: import public
> problem samples into the local workbench and submit the exact locally tested
> C++ source through the user's authenticated Codeforces session.

Permission justification:

- `codeforces.com`: read problem titles, limits, and samples after
  `cfx PROBLEM`; fill and post the submission selected by `cfx submit`.
- `m1.codeforces.com`, `m2.codeforces.com`, `m3.codeforces.com`, and
  `mirror.codeforces.com`: read only the requested public problem when the main
  Codeforces host does not serve its statement.
- `127.0.0.1`: communicate with one short-lived, loopback-only `cfx`
  listener protected by an unguessable operation token.
- `storage`: retain a pending fetch port, token, and problem path during mirror
  navigation, or the Codeforces handle, target, recent prior submission IDs,
  and submission time during submission redirects, only in extension-owned,
  in-memory session storage; each record is keyed by tab and operation and then
  deleted.
- `alarms`: schedule one local, one-shot cleanup at that record's expiry so it
  is deleted even if the submitting tab closes or never returns; the alarm name
  contains only the extension storage key and is cleared on earlier removal.

Data disclosures: personally identifiable information (Codeforces username),
website content, browsing activity limited to the active Codeforces workflow,
form data, and user-generated content (submitted source). Processing is limited
to the user's device and Codeforces; there is no developer collection,
retention, sale, advertising, unrelated use, human access, or remote code.

Privacy policy URL:

> https://github.com/njlane314/cfx/blob/main/src/browser/PRIVACY.md

Remote code:

> No. All executable JavaScript is included in the uploaded extension package.

Reviewer test instructions should link this repository and describe:

```text
make install
cfx 71A
cfx submit
```

The reviewer must be signed in to Codeforces to exercise a live submission. A
problem fetch can be tested without an account. The extension intentionally has
no toolbar popup; its visible result is the CLI output shown in the listing
screenshot. The complete no-network fixture can be reproduced with
`src/browser/demo/build.sh`.

## Current policy references

- [Publish in the Chrome Web Store](https://developer.chrome.com/docs/webstore/publish/)
- [Complete your listing information](https://developer.chrome.com/docs/webstore/cws-dashboard-listing/)
- [Creating a great listing page](https://developer.chrome.com/docs/webstore/best-listing)
- [Chrome Web Store user-data policy](https://developer.chrome.com/docs/webstore/program-policies/user-data-faq)
- [July 2026 policy update](https://developer.chrome.com/blog/cws-policy-updates-2026/),
  whose announced enforcement date is 1 August 2026. The listing's prominent
  disclosure is written for that policy as well as the current dashboard fields.
