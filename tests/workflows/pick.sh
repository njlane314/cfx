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
    "CFX_HANDLE=alice"
    "CFX_STATE_ROOT=$state"
    "EDITOR=false"
    "CFX_BROWSER=false"
)

output=$(env "${environment[@]}" "$repo_root/cfx" --root "$archive" \
    pick --rating 1200 --tag dp)
grep -Fq 'No eligible problems within ±200; widened to ±300.' <<<"$output"
grep -Fq '91004D — Far Match [1500]' <<<"$output"
grep -Fq 'https://codeforces.com/contest/91004/problem/D' <<<"$output"
grep -Fq 'Start with: cfx 91004D' <<<"$output"
! grep -Fq 'secret spoiler' <<<"$output"
test ! -e "$archive/codeforces/91004/D"
test -z "$(git -C "$archive" status --porcelain)"

shown=$(env "${environment[@]}" "$repo_root/cfx" --root "$archive" \
    pick --rating 1200 --tag dp --show-tags)
grep -Fq '(dp, secret spoiler)' <<<"$shown"

widened_quiet=$(env "${environment[@]}" "$repo_root/cfx" --root "$archive" \
    pick --rating 1200 --tag dp --quiet 2>&1)
[[ $widened_quiet == 91004D ]]

quiet=$(env "${environment[@]}" "$repo_root/cfx" --root "$archive" \
    pick --handle alice --rating 1300 --tag choice --count 5 --quiet)
[[ $(sort <<<"$quiet") == $'91101A\n91102B\n91103C\n91104D\n91105E' ]]

help=$($repo_root/cfx help pick)
for option in --rating --count --tag --show-tags --quiet --handle; do
    grep -Fq -- "$option" <<<"$help"
done
