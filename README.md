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

`71A` is the canonical spelling for a Codeforces problem:

```sh
probs get 71A
cd problems/cf/71/A

$EDITOR solution.cpp
probs test
probs test --checked
probs bundle > submission.cpp
probs stress  # once stress/gen.cpp and stress/brute.cpp exist
probs submit
```

`get` also accepts a contest number such as `probs get 2227`. Run `probs cc`
in another terminal before sending a problem from Competitive Companion; its
sample tests are kept in `samples/`, separate from handwritten regression
cases in `cases/`.

Commands infer the problem inside `problems/cf/<contest>/<index>/`, or accept
it explicitly:

```sh
probs test 71A
probs bundle A.71
probs test A 71
```

Codeforces URLs and the old `A.71` spelling remain accepted. Existing flat
workspaces in `solutions/A.71.cpp` and `tests/A.71/` continue to work, so
problems can move to the new layout only when they are touched.

During the transition, `probs new` aliases `get`, `probs run` aliases `test`,
and `probs rerun` uses the same cached `test` engine. The former global wrapper
commands are not installed.

Builds are content-addressed under `.build/`; `test --rebuild` bypasses a
cached build. `submit` prepares and validates a checked bundle, then hands the
authenticated submission step to the browser. The repository never stores a
Codeforces password or session cookie.

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
