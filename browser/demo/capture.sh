#!/usr/bin/env bash

set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
fixtures=$repo_root/tests/tooling/fixtures
output=${1:-$repo_root/.build/demo/capture.txt}
work=$(mktemp -d "${TMPDIR:-/tmp}/cfx-demo-capture.XXXXXX")
trap 'rm -rf "$work"' EXIT

make -s -C "$repo_root" build
mkdir -p "$work/root/templates" "$work/root/include" "$(dirname -- "$output")"
cp "$repo_root/templates/solution.cpp" "$work/root/templates/solution.cpp"
cp -R "$repo_root/include/cp" "$work/root/include/cp"

browser_environment=(
    "CFX_BROWSER=$fixtures/browser.sh"
    "CFX_TEST_BROWSER_LOG=$work/browser.log"
    "CFX_TEST_PROBLEM_PACKAGE=$script_dir/fixture/problem.json"
    "CFX_TEST_SUBMISSION_PAYLOAD=$work/submission.json"
    "CFX_CHROME_EXTENSION_ID=abcdefghijklmnopabcdefghijklmnop"
)

fetch=$(
    env "${browser_environment[@]}" \
        "CFX_TEST_EDITOR_LOG=$work/editor.log" \
        "EDITOR=$fixtures/editor.sh" \
        "$repo_root/bin/cfx" --root "$work/root" 71A
)
cp "$script_dir/fixture/solution.cpp" "$work/root/problems/cf/71/A/solution.cpp"
submit=$(
    cd "$work/root"
    env "${browser_environment[@]}" CFX_STD=c++20 \
        "CFX_API_BASE=file://$script_dir/fixture/api" \
        "$repo_root/bin/cfx" --root "$work/root" submit
)

{
    printf '%s\n' '[fetch]' '$ cfx 71A' "$fetch"
    printf '%s\n' '[source]'
    sed -n '/^void solve()/,/^}/p' "$script_dir/fixture/solution.cpp"
    printf '%s\n' '[submit]' '$ cfx submit'
    sed -E \
        -e 's#^1/1 tests passed.*#1/1 tests passed#' \
        -e '/^Judging wait:/d' \
        <<<"$submit"
} >"$output"

echo "$output"
