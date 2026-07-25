#!/usr/bin/env bash

set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
if [[ -n ${CXX:-} ]]; then
    compiler=$CXX
elif [[ -x /opt/homebrew/opt/llvm/bin/clang++ ]]; then
    compiler=/opt/homebrew/opt/llvm/bin/clang++
elif [[ -x /usr/local/opt/llvm/bin/clang++ ]]; then
    compiler=/usr/local/opt/llvm/bin/clang++
else
    compiler=c++
fi
if [[ -x $compiler ]]; then
    compiler_command=("$compiler")
else
    IFS=' ' read -r -a compiler_command <<<"$compiler"
fi
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/cfx-library.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT
cfx_std=${CFX_STD:-c++20}

for source in "$script_dir"/test_*.cpp; do
    name=$(basename "$source" .cpp)
    "${compiler_command[@]}" \
        "-std=$cfx_std" \
        -Wall \
        -Wextra \
        -Wpedantic \
        -Werror \
        -I"$repo_root/include" \
        "$source" \
        -o "$build_dir/$name"
    "$build_dir/$name"
done

"${compiler_command[@]}" \
    "-std=$cfx_std" \
    -Wall \
    -Wextra \
    -Wpedantic \
    -Werror \
    -I"$repo_root/include" \
    "$repo_root/templates/solution.cpp" \
    -o "$build_dir/solution-template"

echo "library tests passed"
