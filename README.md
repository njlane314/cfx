# CFX

[![Build](https://github.com/njlane314/cfx/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/njlane314/cfx/actions/workflows/ci.yml)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C)

`cfx` is the small, auditable, two-command Codeforces workflow.

It fetches a problem into an ordinary C++ workspace, runs every local test,
submits the exact tested source through your signed-in browser, and reports the
Codeforces verdict. There is no cfx account, hosted service, or credential
store.

## USAGE

```sh
cfx 71A
# edit solution.cpp
cfx submit
```

`cfx 71A` fetches the problem and samples, creates a workspace, opens
`solution.cpp` in your editor, and remembers the problem. `cfx submit` runs
every saved sample and case, checked-compiles the exact bundled source, submits
it through Chrome, and waits for the verdict.

## INSTALL

Requires Bash, a C++20 compiler, `make`, `curl`, and Chrome.

Homebrew, after the first signed release and tap are published:

```sh
brew install njlane314/cfx/cfx
```

From source today:

```sh
make install
```

This installs `cfx` in `~/.local/bin`.

## CHROME SETUP

The connector is prepared for public Chrome Web Store distribution. Until its
listing is approved, install it locally once:

1. Open `chrome://extensions` in the Chrome profile used for Codeforces.
2. Enable **Developer mode**.
3. Choose **Load unpacked** and select this repository's `browser/` directory.
4. Check that **cfx — Codeforces workflow connector** is enabled, then sign in
   to Codeforces.

Reload the extension after pulling connector updates. Passwords and session
cookies remain in Chrome; `cfx` never stores them.

The connector and its loopback protocol are deliberately small enough to audit.
Every request is tied to one explicit CLI operation, an extension identity, and
a short-lived random token. See [browser/SECURITY.md](browser/SECURITY.md) for
the security model and [browser/PRIVACY.md](browser/PRIVACY.md) for the data
flow.

Without the connector, `cfx submit` copies the tested bundle and opens the
submission page. Use `cfx submit --manual` to request that path directly.

## LIBRARY

The solution template contains only stream setup and `solve()`. Reusable,
header-only components live under `include/cp/` and are included selectively:

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
browser/demo/build.sh
```

This runs a real, isolated `cfx 71A` fetch-and-submit workflow, then renders its
captured output as a silent 20-second 1080p demonstration and the Chrome Web
Store artwork under `.build/browser/store/`. It never contacts Codeforces or
submits externally. See [browser/STORE.md](browser/STORE.md) for publication
details.

## LICENSE

[MIT](LICENSE)
