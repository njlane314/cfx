# Privacy policy

The cfx connector acts only when the local `cfx` command opens a Codeforces
problem or submission page for an explicit fetch or submit action, or when
Codeforces redirects that submission action to its sign-in page.

It handles the following data:

- public Codeforces problem metadata and sample tests;
- the selected problem, language, and exact locally tested source code;
- the signed-in Codeforces handle, submission target and time, recent prior
  submission IDs, and resulting submission ID.

Problem data travels from Codeforces to the user's local `cfx` process through
a short-lived listener on `127.0.0.1`. During submission, source code is shared
only with Codeforces through the user's existing signed-in browser session. The
confirmed submission identity returns only to that local process, which obtains
the verdict and resource use from the public Codeforces API. Those are the only
two recipients: the user-controlled local process and Codeforces. No data is
shared with the cfx developer or another third party. The connector does not
read or store passwords or authentication cookies.

The extension has no analytics, advertising, remote code, or developer-operated
server. It does not sell user data or share it for unrelated purposes. During
official-mirror navigation it temporarily keeps the fetch port, token, and
problem path in Chrome's in-memory extension session storage. During a
submission redirect it keeps only the pending submission operation. Each is
deleted after completion or expiry. Files intentionally imported or prepared
by `cfx` remain in the user's local workbench; Codeforces retains submitted
source according to its own policies. Chrome schedules a one-shot extension
alarm for expiry; it performs no periodic monitoring and the alarm contains
only the extension storage key.

Use of information is limited to the connector's user-facing purpose. The use
of information received by the connector adheres to the Chrome Web Store User
Data Policy, including its Limited Use requirements.

Questions or deletion requests can be filed in the
[cfx issue tracker](https://github.com/njlane314/cfx/issues). Local files can
be deleted directly by the user; this project holds no server-side copy.
