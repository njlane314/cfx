#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
fixtures=$repo_root/tests/tooling/fixtures/api-pick
work=$(mktemp -d "${TMPDIR:-/tmp}/cfx-pick.XXXXXX")
trap 'rm -rf "$work"' EXIT

archive=$work/archive
state=$work/state
mkdir -p "$archive/codeforces/91002/B"
printf 'int main() {}\n' >"$archive/codeforces/91002/B/solution.cpp"
git -C "$archive" init -q -b main
git -C "$archive" config user.name 'cfx tests'
git -C "$archive" config user.email 'cfx@example.invalid'
git -C "$archive" add .
git -C "$archive" commit -q -m 'Initialise fixture archive'

environment=(
    "CFX_API_BASE=file://$fixtures"
    "CFX_STATE_ROOT=$state"
    "EDITOR=false"
    "CFX_BROWSER=false"
)

! missing=$(env "${environment[@]}" CFX_STATE_ROOT="$work/empty-state" \
    "$repo_root/cfx" --root "$archive" pick 2>&1)
grep -Fq 'pass a Codeforces handle the first time' <<<"$missing"

output=$(env "${environment[@]}" "$repo_root/cfx" --root "$archive" pick alice)
[[ $output == $'91004D — Ladder Step [1600]\nhttps://codeforces.com/contest/91004/problem/D\nStart with: cfx 91004D' ]]
[[ $(<"$state/handle") == alice && $(wc -c <"$state/handle") -eq 6 ]]
remembered=$(env "${environment[@]}" "$repo_root/cfx" --root "$archive" pick)
[[ $remembered == "$output" ]]
test ! -e "$archive/codeforces/91004/D"
test -z "$(git -C "$archive" status --porcelain)"

help=$($repo_root/cfx help pick)
grep -Fq 'usage: cfx pick [HANDLE]' <<<"$help"
for removed in --rating --count --tag --show-tags --quiet --handle CFX_HANDLE; do
    ! grep -Fq -- "$removed" <<<"$help"
done
