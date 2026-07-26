# CFX

[![Build](https://github.com/njlane314/cfx/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/njlane314/cfx/actions/workflows/ci.yml)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C)

`cfx` is the small, auditable, two-command Codeforces workflow.

It fetches a problem into an ordinary Git-backed solution archive, runs every
local test, submits the exact tested source through your signed-in browser, and
reports the Codeforces verdict. There is no cfx account, hosted service, or
credential store.

## INSTALL

Requires Bash, a C++20 compiler, `make`, `curl`, and Chrome.

From the source checkout:

```sh
make install
command -v cfx
man cfx
```

This installs `cfx` in `~/.local/bin` and its manual in
`~/.local/share/man/man1`. Ensure `~/.local/bin` is on `PATH`.

## SETUP

Create a solution archive:

```sh
git init -b main solutions
cd solutions
git config cfx.record commit
```

The archive name, path, and remote are arbitrary. `cfx.record=commit` is
required for local acceptance commits.

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

## USE

From the solution archive:

```sh
cfx 71A
# edit solution.cpp
cfx submit

# after final judging
cfx sync 71
git push
```

For a problem with multiple valid outputs:

```sh
cfx test --remote-check 2250B
cfx submit --remote-check 2250B
```

`cfx 71A` fetches the problem and samples, creates `codeforces/71/A/`, opens
`solution.cpp`, and remembers the problem. If Codeforces is unavailable, the
connector tries its official contest mirrors. `cfx submit` tests and
checked-compiles the exact bundled source, submits it with the contest form in
Chrome, and waits for the verdict. Only `OK` on `TESTS` is archived as
Accepted. With `cfx.record=commit`, it is committed immediately. `PRETESTS` and
known pending submissions preserve their exact source and receipt in external
state.

`--remote-check` is explicit per command. Builds and runs must succeed within
their limits; only comparison with the sample answer is skipped. Local success
does not establish correctness. Codeforces decides it.

After final judging, `cfx sync [CONTEST|PROBLEM]` reconciles the saved submission
IDs. A numeric contest is the ID in `/contest/<id>/`, not the displayed round
number. Sync is idempotent, never resubmits, and requires `cfx.record=commit` to
make narrow local commits. Submit and sync return 0 on success, 1 for a final
non-Accepted verdict, and 2 while pending, requiring manual completion, or
failing operationally. Never push a public solution archive during a round;
`cfx` never pushes.

## FILES

`cfx` creates the archive as needed:

```text
solutions/
├── .cfx/solution.cpp               optional starter template
├── include/                        optional personal headers
└── codeforces/<contest>/<index>/
    ├── solution.cpp
    ├── problem.json
    ├── cases/                      optional authored tests
    ├── stress/                     optional generator and brute force
    └── submissions/                exact accepted bundle, when needed
```

Only durable, authored files belong here. Fetched samples, builds, run output,
failures, prepared submissions, and receipts remain external. Empty optional
directories need no placeholder files. A tracked `.cfx/solution.cpp` overrides
the packaged starter.

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
