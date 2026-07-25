# Privacy policy

The cfx connector acts only when the local `cfx` command opens a
Codeforces problem or submission page for an explicit fetch or submit action,
or when Codeforces redirects that submission action to its sign-in page.

It handles the following data:

- public Codeforces problem metadata and sample tests;
- the selected problem, language, and exact locally tested source code;
- the resulting Codeforces submission ID, URL, verdict, passed-test count,
  execution time, memory use, and observed judging wait.

Problem data travels from Codeforces to the `cfx` process through a
short-lived listener on `127.0.0.1`. During submission, source code is sent
directly to Codeforces through the user's existing signed-in browser session.
The result returns to the same loopback listener. The connector does not read
or store passwords or authentication cookies.

The extension has no analytics, advertising, remote code, or developer-operated
server. It does not sell user data or share it for unrelated purposes. It keeps
no extension-side history or storage. Files intentionally imported or prepared
by `cfx` remain in the user's local workbench; Codeforces retains submitted
source according to its own policies.

Use of information is limited to the connector's user-facing purpose and
complies with the Chrome Web Store User Data Policy, including its Limited Use
requirements.

Questions or deletion requests can be filed in the repository's issue tracker.
Local files can be deleted directly by the user; this project holds no
server-side copy.
