# cfx

[![Build](https://github.com/njlane314/cfx/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/njlane314/cfx/actions/workflows/ci.yml)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C)

`cfx` — prepare, test, and submit Codeforces solutions.

## Usage

```sh
cfx 71A
# edit solution.cpp
cfx submit
```

`cfx 71A` fetches the problem and samples, creates a workspace, opens
`solution.cpp` in your editor, and remembers the problem. `cfx submit` runs
every saved sample and case, checked-compiles the exact bundled source, submits
it through Chrome, and waits for the verdict.

## Install

Requires Bash, a C++20 compiler, `make`, `curl`, and Chrome.

```sh
make install
```

This installs `cfx` in `~/.local/bin`.

## Chrome setup

Install the local connector once:

1. Open `chrome://extensions` in the Chrome profile used for Codeforces.
2. Enable **Developer mode**.
3. Choose **Load unpacked** and select this repository's `browser/` directory.
4. Check that **cfx connector** is enabled, then sign in to Codeforces.

Reload the extension after pulling connector updates. Passwords and session
cookies remain in Chrome; `cfx` never stores them.

Without the connector, `cfx submit` copies the tested bundle and opens the
submission page. Use `cfx submit --manual` to request that path directly.

## Library

The solution template contains only stream setup and `solve()`. Reusable,
header-only components live under `include/cp/` and are included selectively:

```cpp
#include "cp/ds/fenwick.hpp"
```

Each header is self-contained and uses the `cp` namespace. `cfx bundle` expands
local headers into submission-ready source.

## Checks

```sh
make verify
```

Run `cfx help` for advanced commands.
