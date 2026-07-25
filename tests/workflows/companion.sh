#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
fixtures=$repo_root/tests/tooling/fixtures
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/cfx-companion-workflow.XXXXXX")
cc_pid=
cleanup() {
    if [[ -n $cc_pid ]]; then
        kill "$cc_pid" 2>/dev/null || true
        wait "$cc_pid" 2>/dev/null || true
    fi
    rm -rf "$build_dir"
}
trap cleanup EXIT

sandbox=$build_dir/workspace
mkdir -p "$sandbox/templates" "$sandbox/include"
cp "$repo_root/templates/solution.cpp" "$sandbox/templates/solution.cpp"
cp -R "$repo_root/include/cp" "$sandbox/include/cp"

port=$((32000 + ($$ % 20000)))
"$repo_root/bin/cfx" --root "$sandbox" cc --once --port "$port" \
    >"$build_dir/cc.out" 2>"$build_dir/cc.err" &
cc_pid=$!
curl \
    --silent \
    --fail \
    --retry 20 \
    --retry-connrefused \
    --retry-delay 0 \
    -H 'Content-Type: application/json' \
    --data-binary "@$fixtures/companion.json" \
    "http://127.0.0.1:$port/" \
    >"$build_dir/cc.response"
wait "$cc_pid"
cc_pid=

grep -q 'imported 99992B' "$build_dir/cc.response"
test -f "$sandbox/problems/cf/99992/B/samples/01.in"
test -f "$sandbox/problems/cf/99992/B/samples/01.out"

echo "Competitive Companion workflow passed"
