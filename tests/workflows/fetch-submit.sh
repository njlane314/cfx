#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
fixtures=$repo_root/tests/tooling/fixtures
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/cfx-fetch-submit.XXXXXX")
build_dir=$(cd "$build_dir" && pwd -P)
trap 'rm -rf "$build_dir"' EXIT

wait_for_log() {
    local pattern=$1
    local path=$2
    local attempts=0
    while ! grep -q "$pattern" "$path" 2>/dev/null; do
        attempts=$((attempts + 1))
        if ((attempts >= 100)); then
            echo "fetch/submit workflow: browser log never contained $pattern" >&2
            return 1
        fi
        sleep 0.02
    done
}

sandbox=$build_dir/workspace
state=$build_dir/state
mkdir -p "$sandbox/templates" "$sandbox/include"
cp "$repo_root/assets/templates/solution.cpp" "$sandbox/templates/solution.cpp"
cp -R "$repo_root/assets/include/cp" "$sandbox/include/cp"
export CFX_STATE_ROOT=$state

git -C "$sandbox" init -q -b main
git -C "$sandbox" config user.name 'cfx tests'
git -C "$sandbox" config user.email 'cfx@example.invalid'
git -C "$sandbox" config cfx.record commit
printf '# fixture\n' >"$sandbox/README.md"
git -C "$sandbox" add README.md
git -C "$sandbox" commit -q -m 'Initialise fixture archive'

browser_log=$build_dir/browser.log
editor_log=$build_dir/editor.log
submission_payload=$build_dir/submission.json
clipboard_payload=$build_dir/clipboard.cpp
extension_id=abcdefghijklmnopabcdefghijklmnop
cfx_std=${CFX_STD:-c++20}
case $cfx_std in
    c++17 | gnu++17) language='GNU C++17' ;;
    c++20 | gnu++20) language='GNU C++20' ;;
    c++23 | gnu++23) language='GNU C++23' ;;
    *) language=$cfx_std ;;
esac

browser_environment=(
    "CFX_BROWSER=$fixtures/browser.sh"
    "CFX_TEST_BROWSER_LOG=$browser_log"
    "CFX_TEST_PROBLEM_PACKAGE=$fixtures/browser-package.json"
    "CFX_TEST_SUBMISSION_PAYLOAD=$submission_payload"
    "CFX_CHROME_EXTENSION_ID=$extension_id"
    "CFX_TEST_EXPECT_PAGE_URL=https://codeforces.com/contest/99993/submit"
)

start_output=$(
    env \
        "${browser_environment[@]}" \
        "CFX_TEST_EDITOR_LOG=$editor_log" \
        "EDITOR=$fixtures/editor.sh" \
        "$repo_root/cfx" --root "$sandbox" 99993C
)
grep -q 'Fetched 99993C — Browser bridge problem' <<<"$start_output"
grep -q 'Imported 2 samples' <<<"$start_output"
grep -q 'Opened codeforces/99993/C/solution.cpp' <<<"$start_output"
problem_dir=$sandbox/codeforces/99993/C
sample_dir=$state/codeforces/99993/C/samples
test -f "$problem_dir/problem.json"
test -f "$sample_dir/01.in"
test -f "$sample_dir/02.out"
grep -q "$problem_dir/solution.cpp" "$editor_log"
grep -q '^fetch$' "$browser_log"
grep -qx '99993C' "$state/current-problem"

cp "$fixtures/sum.cpp" "$problem_dir/solution.cpp"
cp "$fixtures/10.in" "$sample_dir/01.in"
env \
    "${browser_environment[@]}" \
    "CFX_TEST_EDITOR_LOG=$editor_log" \
    "EDITOR=$fixtures/editor.sh" \
    "$repo_root/cfx" --root "$sandbox" 99993C >/dev/null
cmp "$fixtures/sum.cpp" "$problem_dir/solution.cpp"
cmp "$fixtures/02.in" "$sample_dir/01.in"

submit_output=$(
    cd "$sandbox"
    env \
        "${browser_environment[@]}" \
        "CFX_API_BASE=file://$fixtures/api-ok" \
        "$repo_root/cfx" --root "$sandbox" submit
)
grep -q '2/2 tests passed' <<<"$submit_output"
grep -q 'Checked build passed' <<<"$submit_output"
grep -q "Submitted 99993C as $language" <<<"$submit_output"
grep -q 'Submission: 123456789' <<<"$submit_output"
grep -q 'URL: https://codeforces.com/contest/99993/submission/123456789' <<<"$submit_output"
grep -q 'Participation: PRACTICE' <<<"$submit_output"
grep -q 'Verdict: Accepted' <<<"$submit_output"
grep -q 'Tests passed: 20' <<<"$submit_output"
grep -q 'Time: 46 ms' <<<"$submit_output"
grep -q 'Memory: 100.0KiB' <<<"$submit_output"
grep -Eq '^Judging wait: [0-9]+\.[0-9]{3}s$' <<<"$submit_output"
grep -q 'Recorded: 99993C' <<<"$submit_output"
grep -q '"target":"99993C"' "$submission_payload"
grep -q "\"language\":\"$language\"" "$submission_payload"
grep -q '"source":' "$submission_payload"
grep -q '#include' "$submission_payload"
grep -q '^submit$' "$browser_log"
grep -q '^Solve Codeforces 99993C — Browser bridge problem$' \
    < <(git -C "$sandbox" log -1 --format=%s)
git -C "$sandbox" log -1 --format=%B | grep -q '^Codeforces-Submission: 123456789$'
test -f "$problem_dir/submissions/123456789.cpp"
test -f "$state/receipts/99993C/123456789/receipt.json"
grep -q '"schemaVersion": 2' "$state/receipts/99993C/123456789/receipt.json"
grep -q '"participantType": "PRACTICE"' "$state/receipts/99993C/123456789/receipt.json"
grep -q '"testset": "TESTS"' "$state/receipts/99993C/123456789/receipt.json"
grep -q '"state": "accepted"' "$state/receipts/99993C/123456789/receipt.json"
grep -q '"sourceDigest"' "$state/receipts/99993C/123456789/receipt.json"
git -C "$sandbox" show --format= --name-only HEAD | grep -q '^codeforces/99993/C/solution.cpp$'
git -C "$sandbox" show --format= --name-only HEAD | \
    grep -q '^codeforces/99993/C/submissions/123456789.cpp$'

if tle_output=$(
    cd "$sandbox"
    env \
        "${browser_environment[@]}" \
        "CFX_API_BASE=file://$fixtures/api-tle" \
        "$repo_root/cfx" --root "$sandbox" submit 2>&1
); then
    echo 'fetch/submit workflow: remote TLE was reported as success' >&2
    exit 1
else
    tle_status=$?
fi
[[ $tle_status == 1 ]]
grep -q 'Submission: 123456789' <<<"$tle_output"
grep -q 'Verdict: Time Limit Exceeded' <<<"$tle_output"
grep -q 'Tests passed: 2' <<<"$tle_output"
grep -q 'Time: 1000 ms' <<<"$tle_output"
grep -q 'Memory: 200.0KiB' <<<"$tle_output"
grep -Eq '^Judging wait: [0-9]+\.[0-9]{3}s$' <<<"$tle_output"

"$repo_root/cfx" --root "$sandbox" get 99992A >/dev/null
conflict_dir=$sandbox/codeforces/99992/A
: >"$browser_log"
if conflict_output=$(
    cd "$conflict_dir"
    env \
        "${browser_environment[@]}" \
        "$repo_root/cfx" --root "$sandbox" submit 2>&1
); then
    echo 'fetch/submit workflow: conflicting current problems were accepted' >&2
    exit 1
fi
grep -q 'submission target is ambiguous: current directory is 99992A but current problem is 99993C' \
    <<<"$conflict_output"
test ! -s "$browser_log"

: >"$browser_log"
if manual_output=$(
    cd "$conflict_dir"
    env \
        "${browser_environment[@]}" \
        "CFX_CLIPBOARD=$fixtures/clipboard.sh" \
        "CFX_TEST_CLIPBOARD=$clipboard_payload" \
        "$repo_root/cfx" --root "$sandbox" submit --manual 99993C 2>&1
); then
    echo 'fetch/submit workflow: manual handoff was reported as accepted' >&2
    exit 1
else
    manual_status=$?
fi
[[ $manual_status == 2 ]]
grep -q '2/2 tests passed' <<<"$manual_output"
grep -q 'Checked build passed' <<<"$manual_output"
grep -q 'Copied tested bundle .* to the clipboard' <<<"$manual_output"
grep -q 'Opened Codeforces submission page for 99993C' <<<"$manual_output"
grep -q "Paste and submit as $language" <<<"$manual_output"
wait_for_log '^manual$' "$browser_log"
artifact=$(find "$state/submissions/prepared" -name submission.cpp -print -quit)
cmp "$artifact" "$clipboard_payload"

: >"$browser_log"
if fallback_output=$(
    cd "$problem_dir"
    env \
        "${browser_environment[@]}" \
        "CFX_CLIPBOARD=$fixtures/clipboard.sh" \
        "CFX_TEST_CLIPBOARD=$clipboard_payload" \
        CFX_TEST_SKIP_CONNECTOR=1 \
        "$repo_root/cfx" --root "$sandbox" submit 2>&1
); then
    echo 'fetch/submit workflow: connector fallback was reported as accepted' >&2
    exit 1
else
    fallback_status=$?
fi
[[ $fallback_status == 2 ]]
grep -q 'Chrome connector unavailable; using manual submission' <<<"$fallback_output"
grep -q 'Copied tested bundle .* to the clipboard' <<<"$fallback_output"
grep -q '^unavailable$' "$browser_log"
wait_for_log '^manual$' "$browser_log"

: >"$browser_log"
cp "$fixtures/wrong.cpp" "$problem_dir/solution.cpp"
if (
    cd "$problem_dir"
    env \
        "${browser_environment[@]}" \
        "$repo_root/cfx" --root "$sandbox" submit
) >/dev/null 2>&1; then
    echo 'fetch/submit workflow: failing source reached the browser' >&2
    exit 1
fi
test ! -s "$browser_log"

printf '99990A\n' >"$state/current-problem"
: >"$browser_log"
if stale_output=$(
    cd "$sandbox"
    env \
        "${browser_environment[@]}" \
        "$repo_root/cfx" --root "$sandbox" submit 2>&1
); then
    echo 'fetch/submit workflow: stale current problem was accepted' >&2
    exit 1
fi
grep -q 'current problem 99990A has no solution' <<<"$stale_output"
test ! -s "$browser_log"
test ! -e "$sandbox/.build"
test ! -e "$sandbox/.cfx"

echo 'fetch and submit workflow passed'
