# Browser connector security

The connector is a narrow bridge between one explicit `cfx` command and one
top-level Codeforces tab. It does not expose a general local HTTP API.

Each operation binds an IPv4 loopback socket on a random port and puts a fresh
256-bit token in the URL fragment. Fragments are not sent to Codeforces, and the
content script removes the fragment before its first asynchronous operation.
The listener accepts only the tokenised route for the active command, the exact
route method and body type, and the configured extension identity. Requests
with an `Origin` must carry the matching extension origin; originless extension
fetches must carry `Sec-Fetch-Site: none`. Request headers and bodies are
bounded before they are read; responses are non-cacheable and carry only the
matching extension CORS origin.

The extension accepts messages only from its own top-level content script on an
explicit allowlist of Codeforces paths. The exact official contest mirrors are
limited to problem-fetch routes; submission remains restricted to
`https://codeforces.com`. Its service worker enforces the same route/method and
byte limits, refuses loopback redirects, and reads responses with a hard byte
limit. Mirror navigation keeps only the fetch port, token, and exact problem
path in `chrome.storage.session`, keyed by tab and operation. Pending fetch and
submission state is removed after completion; fetch state expires with the
browser bridge after 370 seconds, and submission state has a one-shot 60-second
expiry.

Submission automation accepts only `https://codeforces.com` forms posting to
the expected submission path, rejects submit-control action or method
overrides, requires a CSRF token, and records the signed-in handle only from a
same-origin profile link. A new submission ID must be absent from the
pre-submit snapshot and match the target, handle, and submission-time window in
two consecutive API responses. API redirects, response bytes, and record counts
are bounded before those responses are accepted. The CLI independently confirms
that identity and obtains the verdict from the public Codeforces API.

The boundary does not claim to protect against malware running as the same
local user, a compromised browser or extension, or a compromised Codeforces
origin. Such actors can already read source code or act through the user's
session. Security reports should be filed privately through GitHub's
[security advisory form](https://github.com/njlane314/cfx/security/advisories/new).
