# CFX

[![Build](https://github.com/njlane314/cfx/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/njlane314/cfx/actions/workflows/ci.yml)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C)

`cfx` is the small, auditable, two-command Codeforces workflow.

It fetches a problem into an ordinary Git-backed solution archive, runs every local test,
submits the exact tested source through your signed-in browser, and reports the
Codeforces verdict. There is no cfx account, hosted service, or credential
store.

## USAGE

```sh
cd accepted
cfx 71A
# edit solution.cpp
cfx submit
```

`cfx 71A` fetches the problem and samples, creates `codeforces/71/A/`, opens
`solution.cpp` in your editor, and remembers the problem. `cfx submit` runs
every saved sample and case, checked-compiles the exact bundled source, submits
it through Chrome, and waits for the verdict. Tooling and runtime state remain
outside the archive; only solutions, metadata, authored cases, and stress tools
belong there.

Enable one local commit per Accepted verdict in the solution archive:

```sh
git config cfx.record commit
```

The commit records the Codeforces submission ID, URL, resource use, and source
digest. If bundling changed the source, the exact submitted file is retained
under `submissions/`. `cfx` never pushes.

## INSTALL

Requires Bash, a C++20 compiler, `make`, `curl`, and Chrome.

From source:

```sh
make install
```

This installs `cfx` in `~/.local/bin` and its manual as `man cfx`.

## CHROME SETUP

The connector is prepared for public Chrome Web Store distribution. Until its
listing is approved, install it locally once:

1. Open `chrome://extensions` in the Chrome profile used for Codeforces.
2. Enable **Developer mode**.
3. Choose **Load unpacked** and select this repository's `src/browser/`
   directory.
4. Check that **cfx — Codeforces workflow connector** is enabled, then sign in
   to Codeforces.

Reload the extension after pulling connector updates. Passwords and session
cookies remain in Chrome; `cfx` never stores them.

The connector and its loopback protocol are deliberately small enough to audit.
Every request is tied to one explicit CLI operation, an extension identity, and
a short-lived random token. See
[src/browser/SECURITY.md](src/browser/SECURITY.md) for the security model and
[src/browser/PRIVACY.md](src/browser/PRIVACY.md) for the data flow.

Without the connector, `cfx submit` copies the tested bundle and opens the
submission page. Use `cfx submit --manual` to request that path directly.

## LIBRARY

The solution template contains only stream setup and `solve()`. Reusable,
header-only components live under `assets/include/cp/` and are included
selectively:

```cpp
#include "cp/ds/fenwick.hpp"
```

Each header is self-contained and uses the `cp` namespace. `cfx bundle` expands
local headers into submission-ready source.

## CHECKS

```sh
make verify
```

Run `cfx help` for advanced commands.

## DEMONSTRATION

```sh
src/browser/demo/build.sh
```

This runs a real, isolated `cfx 71A` fetch-and-submit workflow, then renders its
captured output as a silent 20-second 1080p demonstration and the Chrome Web
Store artwork under `.build/browser/store/`. It never contacts Codeforces or
submits externally. See [src/browser/STORE.md](src/browser/STORE.md) for
publication details.

## LICENSE

[MIT](LICENSE)
