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
```

The archive name, path, and remote are arbitrary. Version the authored files
with Git as usual; `cfx` does not commit or push.

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
export CFX_HANDLE=your_handle
cfx pick --rating 1300
# fetch the suggested ID, for example:
cfx 71A
# edit solution.cpp
cfx submit
```

`cfx pick` suggests one unsolved, unarchived problem within 100 rating points,
widening to 200 and then 300 only when necessary. Repeat `--tag TAG` to filter,
use `--count 5` for choices, or `--quiet` for IDs only. Tags remain hidden
unless `--show-tags` is passed. Picking never creates or opens the selected
problem; run `cfx ID` explicitly when ready.

`cfx 71A` fetches the problem and samples, creates `codeforces/71/A/`, opens
`solution.cpp`, and remembers the problem. If Codeforces is unavailable, the
connector tries its official contest mirrors. `cfx submit` tests and
checked-compiles the exact bundled source, submits it with the contest form in
Chrome, and waits for the verdict. Only `OK` on `TESTS` is reported as
Accepted. `PRETESTS` and known pending submissions remain pending.

Submit returns 0 for `OK` on `TESTS`, 1 for a final non-Accepted verdict, and 2
while pending, requiring manual completion, or failing operationally. Never
push a public solution archive during a round; `cfx` never pushes.

## FILES

`cfx` creates the archive as needed:

```text
solutions/
├── .cfx/solution.cpp               optional starter template
├── include/                        optional library submodules
└── codeforces/<contest>/<index>/
    ├── solution.cpp
    ├── problem.json
    └── cases/                      optional authored tests
```

Only durable, authored files belong here. Fetched samples, builds, run output,
and prepared submissions remain external. Empty optional directories
need no placeholder files. A tracked `.cfx/solution.cpp` overrides the packaged
starter.

## LIBRARIES

The solution template contains only stream setup and `solve()`. `cfx` ships no
C++ support libraries. Add the extensionless
[`cp`](https://github.com/njlane314/cp) algorithm library and the single-header
[`peek`](https://github.com/njlane314/peek) diagnostics library independently:

```sh
git submodule add https://github.com/njlane314/cp include/cp
git submodule add https://github.com/njlane314/peek include/peek
```

Include only the components a solution uses. An archive can use either library
without the other:

```cpp
#include <cp/fenwick_tree>
```

`cfx` defines `PEEK_COMPILED=1` for local builds and `PEEK_COMPILED=0` for
submission builds, so a solution only needs:

```cpp
#include <peek.hpp>
```

When compiling outside `cfx`, apply the equivalent policy before the include:

```cpp
#ifdef LOCAL
#define PEEK_COMPILED 1
#else
#define PEEK_COMPILED 0
#endif
#include <peek.hpp>
```

The libraries remain independently versioned while `cfx` expands their
transitive includes into the exact source compiled and submitted. The default
template stays dependency-free.

## CHECKS

Clone with `--recurse-submodules` to include the test-only `tst` dependency.

```sh
make verify
```

Run `cfx help` for advanced commands.

## LICENSE

[MIT](LICENSE)
