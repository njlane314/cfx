# Chrome Web Store release

Build the upload archive from the repository root:

```sh
make browser-package
```

The resulting `.build/browser/cfx-connector-<version>.zip` has
`manifest.json` at its root and contains only runtime files.

## Release checklist

- [ ] Upload archive built by `make browser-package`.
- [ ] Store item is **Unlisted** and uses the **Developer Tools** category.
- [ ] Store public key and item ID have replaced the development values in
  `manifest.json` and `browser/extension-id`.
- [ ] 128 x 128 store icon: `browser/icons/icon-128.png` (96 x 96 mark with
  16 px transparent padding).
- [ ] 1280 x 800 screenshot: `browser/store/screenshot-1280x800.png`.
- [ ] 440 x 280 small promo tile: `browser/store/promo-small-440x280.png`.
- [ ] Privacy policy URL:
  `https://github.com/njlane314/cfx/blob/main/browser/PRIVACY.md`.
- [ ] Website URL: `https://github.com/njlane314/cfx`.
- [ ] Data-use disclosures and permission justifications match the copy below.
- [ ] Reviewer instructions are included and the final ZIP has been tested with
  **Load unpacked**.

## First draft

1. Register a Chrome Web Store developer account and create a new item.
2. Upload the ZIP, choose **Unlisted**, and save it as a draft.
3. In **Package**, copy the item ID and **View public key** value.
4. Replace the development `key` in `manifest.json` with that public key, and
   replace the value in `browser/extension-id` with the item ID.
5. Rebuild and upload the final ZIP.

The tracked development key gives **Load unpacked** a stable ID; it has no
private signing key. Do not publish until the Web Store public key and item ID
have replaced both development values and are committed. Public keys and item
IDs are not secrets; signing keys and account credentials must never enter
this repository.

## Ready-to-paste listing

Name:

> cfx connector

Summary:

> Connect the local cfx workflow to Codeforces for sample import and submission.

Category:

> Developer Tools

Detailed description:

> The cfx connector links explicit commands from the local cfx C++
> workbench to Codeforces in Chrome.
>
> Run `cfx 71A` to create a workspace and import public problem metadata and
> samples. Write and test the solution locally, then run `cfx submit` to send
> the exact tested C++ bundle through your existing signed-in Codeforces tab.
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
- `127.0.0.1`: communicate with one short-lived, loopback-only `cfx`
  listener protected by an unguessable operation token.
- `storage`: retain the pending loopback port and token, Codeforces handle and
  target, recent prior submission IDs, and submission time only in
  extension-owned, in-memory session storage while Codeforces redirects the
  submitting tab; the record is keyed by tab and operation and then deleted.
- `alarms`: schedule one local, one-shot cleanup at that record's expiry so it
  is deleted even if the submitting tab closes or never returns; the alarm name
  contains only the extension storage key and is cleared on earlier removal.

Data disclosures: website content, browsing activity, form data, and
user-generated content. Processing is limited to the user's device and
Codeforces; there is no developer collection, retention, sale, advertising, or
remote code.

Privacy policy URL:

> https://github.com/njlane314/cfx/blob/main/browser/PRIVACY.md

Remote code:

> No. All executable JavaScript is included in the uploaded extension package.

Reviewer test instructions should link this repository and describe:

```text
make install
cfx 71A
cfx submit
```

The reviewer must be signed in to Codeforces to exercise submission. A problem
fetch can be tested without an account. The extension intentionally has no
toolbar popup; its visible result is the CLI output shown in the listing
screenshot.
