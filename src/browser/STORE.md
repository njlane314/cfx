# Chrome Web Store release

Build the upload archive from the repository root:

```sh
make browser-package
```

The resulting `.build/browser/cfx-connector-<version>.zip` has
`manifest.json` at its root and contains only runtime files. Packaging requires
Node.js and `zip`, includes the tracked extension icons, and downloads no
dependency.

## Release checklist

- [ ] Upload archive built by `make browser-package`.
- [ ] Distribution visibility is **Public**, all intended regions are selected,
  and the category is **Developer Tools**.
- [ ] Store public key and item ID have replaced the development values in
  `src/browser/manifest.json` and `src/browser/extension-id`.
- [ ] Listing artwork meets the current Chrome Web Store requirements.
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
   public key, and replace `src/browser/extension-id` with the item ID.
5. Rebuild and upload the final ZIP.
6. Paste the copy below and upload the listing artwork.
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
- `m3.codeforces.com` and `mirror.codeforces.com`: read only the requested
  public problem when the main Codeforces host does not serve its statement.
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
no toolbar popup; its visible result is the CLI output.

## Current policy references

- [Publish in the Chrome Web Store](https://developer.chrome.com/docs/webstore/publish/)
- [Complete your listing information](https://developer.chrome.com/docs/webstore/cws-dashboard-listing/)
- [Creating a great listing page](https://developer.chrome.com/docs/webstore/best-listing)
- [Chrome Web Store user-data policy](https://developer.chrome.com/docs/webstore/program-policies/user-data-faq)
- [July 2026 policy update](https://developer.chrome.com/blog/cws-policy-updates-2026/),
  whose announced enforcement date is 1 August 2026. The listing's prominent
  disclosure is written for that policy as well as the current dashboard fields.
