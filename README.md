# cf-probs

[Codeforces](https://codeforces.com) problem solutions written in C++20.

Files are named `solutions/A.71.cpp`; tests live in `tests/A.71/`.

## Make

```sh
make new A 71
make run A 71
make rerun all
make bundle A 71
make all
make clean
```

`make bundle A 71` writes the single-file source to stdout.
`make rerun all` runs cached binaries in `.build/` without compiling.
