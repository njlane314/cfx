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
common_flags=(
    "-std=$cfx_std"
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    "-I$repo_root/assets/include"
)

headers=(types utility contract debug disjoint_set fenwick_tree segment_tree modint kmp)
for header in "${headers[@]}"; do
    printf '#include <cp/%s>\nint main() {}\n' "$header" |
        "${compiler_command[@]}" "${common_flags[@]}" -x c++ -fsyntax-only -
done

for source in "$script_dir"/test_*.cpp; do
    name=$(basename "$source" .cpp)
    "${compiler_command[@]}" \
        "${common_flags[@]}" \
        "$source" \
        -o "$build_dir/$name"
    "$build_dir/$name"
done

expect_contract() {
    local mode=$1
    local message=$2
    local output=$build_dir/contract-$mode.log
    if bash -c 'ulimit -c 0; "$1" "$2"; exit $?' \
        _ "$build_dir/test_library" "$mode" >"$output" 2>&1; then
        echo "library tests: contract case did not fail: $mode" >&2
        exit 1
    fi
    grep -Fq "cp: $message" "$output"
    grep -Fq '  expected: ' "$output"
    grep -Fq '  at: ' "$output"
}

expect_contract disjoint-size 'disjoint_set: negative size'
expect_contract fenwick-index 'fenwick_tree: invalid position'
expect_contract segment-range 'segment_tree: invalid range'
expect_contract modint-inverse 'modint: value has no multiplicative inverse'

"${compiler_command[@]}" \
    "${common_flags[@]}" \
    "$repo_root/assets/templates/solution.cpp" \
    -o "$build_dir/solution-template"

echo "library tests passed"
