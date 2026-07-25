# cf-probs

A small Codeforces workbench written in C++20 and Bash. One command owns the
workflow; solutions and tests remain ordinary files.

## Requirements

- Bash 3.2 or newer
- a C++20 compiler (Homebrew LLVM is preferred automatically on macOS when present)
- `make`
- `curl` for Codeforces metadata
- Google Chrome for automatic sample import and submission

Linux and macOS are supported. The workbench has no Python dependency.

## Install

From the repository root:

```sh
make install
```

This links `bin/probs` into `~/.local/bin`. Set `PREFIX` to choose another
location, and ensure its `bin` directory is on `PATH`. The launcher compiles
the C++ tool on first use and whenever its sources or headers change.

## Chrome connector

Chrome is the supported browser for automatic submission. Until the unlisted
Chrome Web Store release is published, load the trusted development build once:

1. Open `chrome://extensions`.
2. Enable **Developer mode**.
3. Choose **Load unpacked** and select this repository's `browser/` directory.

Sign in to Codeforces in Chrome. Credentials and session cookies remain in
Chrome and are never stored by this repository. `probs` opens Google Chrome by
default. `CFPROBS_BROWSER` may point to a different Chrome or Chromium
executable; Safari and Firefox are not supported by this connector.

If the connector is unavailable, `probs submit` safely falls back to copying
the exact tested bundle to the clipboard and opening the submission page. Use
`probs submit --manual` to choose that path directly.

Maintainers can build the unlisted Web Store ZIP with
`make browser-package`; publication notes and required disclosures are in
[`browser/STORE.md`](browser/STORE.md). See the connector's
[privacy policy](browser/PRIVACY.md) for its exact local data flow.

## Daily workflow

The daily workflow is two commands:

```sh
probs 71A
# $EDITOR opens problems/cf/71/A/solution.cpp

probs submit
```

The first command fetches the problem metadata and samples, creates its
workspace, opens the solution, and records it as the current problem in ignored
`.build/` state. Fetched tests live in `samples/`; add handwritten regression
tests to `cases/`, which fetching never replaces. `probs submit` uses the
problem in the current directory when there is one, otherwise the recorded
problem. If those two targets conflict, it stops and asks for an explicit ID.
It runs every test, checked-builds and pins the exact source, submits through
the browser session, and reports the submission URL plus any immediate verdict.
Invoking `submit` is the authorization; there is no second confirmation prompt.

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
