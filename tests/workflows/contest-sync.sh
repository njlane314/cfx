#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
fixtures=$repo_root/tests/tooling/fixtures
work=$(mktemp -d "${TMPDIR:-/tmp}/cfx-contest-sync.XXXXXX")
trap 'rm -rf "$work"' EXIT

new_archive() {
    archive=$work/$1
    state=$work/$1-state
    browser_log=$work/$1-browser.log
    submission_payload=$work/$1-submission.json
    problem=$archive/codeforces/99993/C
    receipt=$state/receipts/99993C/123456789

    mkdir -p "$problem" "$state/codeforces/99993/C/samples"
    cat >"$problem/solution.cpp" <<'CPP'
#include <iostream>

int main() {
    long long left = 0;
    long long right = 0;
    std::cin >> left >> right;
    std::cout << left + right << '\n';
}
CPP
    printf '%s\n' '{"id":"99993C","name":"Contest sync","timeLimitMs":1500,"memoryLimitMb":256}' \
        >"$problem/problem.json"
    printf '2 3\n' >"$state/codeforces/99993/C/samples/01.in"
    printf '5\n' >"$state/codeforces/99993/C/samples/01.out"

    git -C "$archive" init -q -b main
    git -C "$archive" config user.name 'cfx tests'
    git -C "$archive" config user.email 'cfx@example.invalid'
    git -C "$archive" config cfx.record commit
    printf '# fixture\n' >"$archive/README.md"
    git -C "$archive" add README.md
    git -C "$archive" commit -q -m 'Initialise fixture archive'
    initial=$(git -C "$archive" rev-parse HEAD)
}

run_cfx() {
    local api=$1
    shift
    env \
        "CFX_STATE_ROOT=$state" \
        "CFX_BROWSER=$fixtures/browser.sh" \
        "CFX_TEST_BROWSER_LOG=$browser_log" \
        "CFX_TEST_SUBMISSION_PAYLOAD=$submission_payload" \
        "CFX_TEST_EXPECT_PAGE_URL=https://codeforces.com/contest/99993/submit" \
        CFX_CHROME_EXTENSION_ID=abcdefghijklmnopabcdefghijklmnop \
        "CFX_API_BASE=file://$api" \
        "$repo_root/cfx" --root "$archive" "$@"
}

submit_pretests() {
    local status
    : >"$browser_log"
    if submit_output=$(run_cfx "$fixtures/api-pretests" submit 99993C 2>&1); then
        echo 'contest sync: pretests were reported as final' >&2
        return 1
    else
        status=$?
    fi
    [[ $status == 2 ]]
    grep -q 'Participation: CONTESTANT' <<<"$submit_output"
    grep -q 'Verdict: Pretests passed' <<<"$submit_output"
    grep -q 'Pending: final judging' <<<"$submit_output"
    grep -q '^submit$' "$browser_log"
    [[ $(git -C "$archive" rev-parse HEAD) == "$initial" ]]
    test -f "$receipt/solution.cpp"
    test -f "$receipt/submission.cpp"
    test ! -e "$receipt/git-commit"
    grep -q '"schemaVersion": 2' "$receipt/receipt.json"
    grep -q '"participantType": "CONTESTANT"' "$receipt/receipt.json"
    grep -q '"testset": "PRETESTS"' "$receipt/receipt.json"
    grep -q '"state": "pretests"' "$receipt/receipt.json"
}

new_archive nonfinal
: >"$browser_log"
if submit_output=$(run_cfx "$fixtures/api-challenges" submit 99993C 2>&1); then
    echo 'contest sync: non-final OK was reported as accepted' >&2
    exit 1
else
    submit_status=$?
fi
[[ $submit_status == 2 ]]
grep -q '^Pending:' <<<"$submit_output"
[[ $(git -C "$archive" rev-parse HEAD) == "$initial" ]]
test ! -e "$receipt/git-commit"
grep -q '"verdict": "OK"' "$receipt/receipt.json"
grep -q '"testset": "CHALLENGES"' "$receipt/receipt.json"
grep -q '"state": "pending"' "$receipt/receipt.json"

new_archive accepted
submit_pretests
: >"$browser_log"
sync_output=$(run_cfx "$fixtures/api-system-ok" sync 99993C)
grep -q '^99993C: Accepted 123456789 \[[0-9a-f]\{12\}\]$' <<<"$sync_output"
test ! -s "$browser_log"
test -f "$receipt/git-commit"
grep -q '"testset": "TESTS"' "$receipt/receipt.json"
grep -q '"state": "accepted"' "$receipt/receipt.json"
[[ $(git -C "$archive" rev-list --count HEAD) == 2 ]]
accepted_commit=$(git -C "$archive" rev-parse HEAD)
rm "$receipt/git-commit"
run_cfx "$work/unavailable-api" sync 99993C >/dev/null
[[ $(git -C "$archive" rev-parse HEAD) == "$accepted_commit" ]]
[[ $(git -C "$archive" rev-list --count HEAD) == 2 ]]
grep -qx "$accepted_commit" "$receipt/git-commit"
sync_output=$(run_cfx "$work/unavailable-api" sync 99993C)
grep -q '^sync: up to date$' <<<"$sync_output"
[[ $(git -C "$archive" rev-list --count HEAD) == 2 ]]

new_archive rejected
submit_pretests
: >"$browser_log"
if sync_output=$(run_cfx "$fixtures/api-tle" sync 99993C 2>&1); then
    echo 'contest sync: failed system tests were reported as accepted' >&2
    exit 1
else
    sync_status=$?
fi
[[ $sync_status == 1 ]]
grep -q '^99993C: Time Limit Exceeded 123456789$' <<<"$sync_output"
test ! -s "$browser_log"
test ! -e "$receipt/git-commit"
grep -q '"verdict": "TIME_LIMIT_EXCEEDED"' "$receipt/receipt.json"
grep -q '"state": "rejected"' "$receipt/receipt.json"
[[ $(git -C "$archive" rev-parse HEAD) == "$initial" ]]
grep -q '^sync: up to date$' < <(run_cfx "$fixtures/api-tle" sync 99993C)

new_archive drift
submit_pretests
cat >"$problem/solution.cpp" <<'CPP'
int main() {
    return 0;
}
CPP
drifted=$(cat "$problem/solution.cpp")
: >"$browser_log"
if sync_output=$(run_cfx "$fixtures/api-system-ok" sync 99993C 2>&1); then
    echo 'contest sync: changed source was recorded' >&2
    exit 1
else
    sync_status=$?
fi
[[ $sync_status == 2 ]]
grep -q 'not recorded (solution changed after submission)' <<<"$sync_output"
test ! -s "$browser_log"
test ! -e "$receipt/git-commit"
[[ $(cat "$problem/solution.cpp") == "$drifted" ]]
[[ $(git -C "$archive" rev-parse HEAD) == "$initial" ]]

cp "$receipt/solution.cpp" "$problem/solution.cpp"
sync_output=$(run_cfx "$fixtures/api-system-ok" sync 99993C)
grep -q '^99993C: Accepted 123456789 \[[0-9a-f]\{12\}\]$' <<<"$sync_output"
test -f "$receipt/git-commit"
[[ $(git -C "$archive" rev-list --count HEAD) == 2 ]]

echo 'contest sync workflow passed'
