# cf-probs

Codeforces workspace for C++20 solutions.

## Synopsis

```sh
make new P=71A
make new C=1985 P=A-H
make run P=1985A
make bundle P=1985A
make submit P=1985A
make all
make clean
```

## Layout

```text
bin/          shell commands
lib/          local C++ headers
solutions/    editable solutions
submissions/  bundled single-file output
tests/        sample tests by problem id
template.cpp  new-problem template
```

## Commands

`make new P=71A`
: create `solutions/71A.cpp` and `tests/71A/`.

`make new C=1985 P=A-H`
: create contest files from `1985A` through `1985H`.

`make run P=1985A`
: bundle, compile, and run `solutions/1985A.cpp`.

`make run`
: run the newest file in `solutions/`.

`make all`
: run every solution.

`make bundle P=1985A`
: write `submissions/1985A.cpp`.

`make submit P=1985A`
: bundle, then submit with `cf` if installed.

`make install`
: symlink `bin/{bundle,new,run,submit}` into `$HOME/.local/bin`.

## Tests

Test files live under `tests/<id>/`.

```text
tests/1985A/case-1.in
tests/1985A/case-1.out
```

If `.out` is missing, `run` prints the program output.

## Environment

```sh
CXX=g++
STD=gnu++20
CXXFLAGS='-O2 -pipe -Wall -Wextra -Wshadow -DLOCAL'
TL=5
```

## Rule

Submit the bundled file in `submissions/`.
