# cf-probs

[Codeforces](https://codeforces.com) problem solutions written in C++20.

Files are named `solutions/A.71.cpp`; tests live in `tests/A.71/`.

## Make

```sh
make new A 71
make run A 71
make check A 71
make rerun all
make bundle A 71
make probe A 71 tests/A.71/case-1.in
make bench A 71 tests/A.71/case-1.in N=5
make case A 71 < input.txt
make stress A 71 GEN=stress/A.gen.cpp BRUTE=stress/A.brute.cpp N=10000
make fail A 71
make meta 71A
make pick tourist 800 strings
make contest 71
make sample A 71
make seen tourist
make solved tourist
make rank tourist
make cc
make all
make clean
```

`make bundle A 71` writes the single-file source to stdout.
`make rerun all` runs cached binaries in `.build/` without compiling.

## Checked builds

`make run` uses `-O2 -pipe -Wall -Wextra -Wshadow -Wformat=2 -DLOCAL`.
`make check A 71` is the slower debug build. It sets `CHECK=1` and adds the
supported parts of the stricter warning/sanitizer set: `-pedantic`,
`-Wfloat-equal`, `-Wconversion`, `-Wlogical-op`, `-Wshift-overflow=2`,
`-Wduplicated-cond`, `-Wcast-qual`, `-Wcast-align`, libstdc++ debug mode,
AddressSanitizer, UBSan, no sanitizer recovery, stack protection, and
`_FORTIFY_SOURCE=2` on Linux.

Use `CHECK=1` with `make probe`, `make bench`, or `make stress` to compile
their solution, generator, and brute-force binaries with the same checks.

## Local testing

`make probe A 71 input.txt` compiles the solution and runs one arbitrary input
without adding it to `tests/`. It saves the input and output under
`.build/stress/A.71/`, so `make fail A 71` can promote that input later.

`make bench A 71 big.in N=5` compiles once, runs the same input repeatedly, and
reports min/avg/max time plus output size.

`make case A 71 < input.txt` writes the next `tests/A.71/case-N.in`.
`make case A 71 input.txt answer.txt` also writes `case-N.out`.

`make stress A 71 GEN=stress/A.gen.cpp BRUTE=stress/A.brute.cpp N=10000`
compiles the solution, generator, and brute force. The generator is called as
`gen SEED`; the first mismatch stops the run and leaves `fail.in`, `out.txt`,
and `ans.txt` under `.build/stress/A.71/`. `GENARGS='...'` appends extra
generator arguments.

## Codeforces data

The API-backed helpers cache under `.build/cf/` and accept `CF_HANDLE`
instead of a handle argument where that makes sense.

```sh
make meta 1600 dp        # problem metadata from problemset.problems
make pick tourist 1600 dp # unsolved suggestions for a handle
make contest 2227        # create solutions/tests for public contest problems
make seen tourist        # attempted/accepted problems for a handle
make solved tourist      # local solution files vs accepted submissions
make rank tourist        # user rating history
make rank contests       # upcoming contests
make rank 566            # rating changes for a contest
```

`make sample A 71` tries public online sample sources and writes
`tests/A.71/case-*.in` plus `.out`. The official API does not expose problem
samples; direct HTML scraping can be blocked by a browser challenge, so
`make cc` also listens for Competitive Companion packages on port 27121. Run
it, open a Codeforces problem in the browser, then send the problem from the
extension.
