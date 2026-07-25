# cfx

`cfx` — prepare, test, and submit Codeforces solutions.

## SYNOPSIS

```sh
cfx 71A
# edit solution.cpp
cfx submit
```

The first command fetches the problem and samples, creates a workspace, opens
`$EDITOR`, and remembers the problem. The second runs every saved sample and
case, checked-compiles the exact bundled source, submits it through Chrome, and
waits for the verdict.

## INSTALL

Requires Bash, a C++20 compiler, `make`, `curl`, and Chrome.

```sh
make install
```

This installs `cfx` in `~/.local/bin`. Set `PREFIX` to choose another prefix
and ensure its `bin` directory is on `PATH`.

## CHROME

Install the local connector once:

1. Open `chrome://extensions` in the Chrome profile used for Codeforces.
2. Enable **Developer mode**.
3. Choose **Load unpacked** and select this repository's `browser/` directory.
4. Check that **cfx connector** is enabled, then sign in to Codeforces.

Reload the extension after pulling connector updates. Passwords and session
cookies remain in Chrome; `cfx` never stores them.

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

## DEVELOPMENT

```sh
make verify
```

Run `cfx help` for advanced commands.
