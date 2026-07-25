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
export CXX=$compiler
if [[ -x $compiler ]]; then
    compiler_command=("$compiler")
else
    IFS=' ' read -r -a compiler_command <<<"$compiler"
fi

build_dir=$(mktemp -d "${TMPDIR:-/tmp}/cfx-tooling.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT
cfx_std=${CFX_STD:-c++20}

if command -v node >/dev/null 2>&1; then
    node "$script_dir/background_test.js"
    node "$script_dir/connector_test.js"
fi

common_flags=(
    "-std=$cfx_std"
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -pthread
    "-I$repo_root/include"
    "-I$repo_root/tools"
    "-I$repo_root/tools/cfx"
)

"${compiler_command[@]}" \
    "${common_flags[@]}" \
    "$script_dir/core_tests.cpp" \
    "$repo_root/tools/cfx/problem.cpp" \
    "$repo_root/tools/cfx/workspace.cpp" \
    "$repo_root/tools/cfx/bundle.cpp" \
    "$repo_root/tools/cfx/file.cpp" \
    -o "$build_dir/core-tests"
"$build_dir/core-tests"

tool_sources=()
for source in "$repo_root"/tools/cfx/*.cpp; do
    [[ $(basename "$source") == main.cpp ]] || tool_sources+=("$source")
done
"${compiler_command[@]}" \
    "${common_flags[@]}" \
    "$script_dir/tool_tests.cpp" \
    "${tool_sources[@]}" \
    -o "$build_dir/tool-tests"
CFX_BROWSER="$script_dir/fixtures/browser.sh" \
CFX_TEST_BROWSER_LOG="$build_dir/browser.log" \
CFX_TEST_SUBMISSION_PAYLOAD="$build_dir/submission.json" \
CFX_CHROME_EXTENSION_ID=abcdefghijklmnopabcdefghijklmnop \
CFX_API_BASE="file://$script_dir/fixtures/api-tle" \
    "$build_dir/tool-tests"

echo "tooling tests passed"
