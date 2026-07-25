# cf-probs

A small Codeforces workbench written in C++20 and Bash. One command owns the
workflow; solutions and tests remain ordinary files.

## Requirements

- Bash 3.2 or newer
- a C++20 compiler (Homebrew LLVM is preferred automatically on macOS when present)
- `make`
- `curl` for Codeforces metadata

Linux and macOS are supported. The workbench has no Python dependency.

## Install

From the repository root:

```sh
make install
```

This links `bin/probs` into `~/.local/bin`. Set `PREFIX` to choose another
location, and ensure its `bin` directory is on `PATH`. The launcher compiles
the C++ tool on first use and whenever its sources or headers change.

## Daily workflow

Set up the browser connector once:

1. Open `chrome://extensions`.
2. Enable **Developer mode**.
3. Choose **Load unpacked** and select this repository's `browser/` directory.

Other Chromium browsers provide the same controls on their extensions page.
Sign in to Codeforces in that browser; credentials and session cookies remain
there and are never stored by this repository.
If that is not your default browser, set `CFPROBS_BROWSER` to an opener
command, for example `export CFPROBS_BROWSER='open -a "Google Chrome"'` on
macOS.

The daily workflow is two commands:

```sh
probs 71A
# $EDITOR opens problems/cf/71/A/solution.cpp

probs submit
```

The first command fetches the problem metadata and samples, creates its
workspace, and opens the solution. Fetched tests live in `samples/`; add
handwritten regression tests to `cases/`, which fetching never replaces.
Inside the workspace, `probs submit` infers the problem, runs every test,
checked-builds and pins the exact source, submits through the browser session,
and reports the submission URL plus any immediate verdict. Invoking `submit`
is the authorization; there is no second confirmation prompt.

`71A` is canonical, while `A.71`, `A 71`, Codeforces URLs, and existing
workspaces remain accepted. Lower-level commands such as `test`, `bundle`, and
`stress` are advanced fallbacks. `probs cc` remains available when importing
with Competitive Companion is preferable.

Use `probs help`, `probs --help`, or `probs COMMAND --help` for command
details.

## Library

Reusable code lives below `include/cp/`, with one primary abstraction per
header and no hidden ambient includes. A new component should emerge from real
use:

```text
solve problem -> reuse fragment -> extract API -> test -> promote
```

Library and tooling checks share one entry point:

```sh
make verify
```

Make is only for building, checking, installing, and cleaning the workbench;
daily problem commands go directly through `probs`.
