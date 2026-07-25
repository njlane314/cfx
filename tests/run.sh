#!/usr/bin/env bash

set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)

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

build_dir=$(mktemp -d "${TMPDIR:-/tmp}/cfprobs-tests.XXXXXX")
build_dir=$(cd "$build_dir" && pwd -P)
trap 'rm -rf "$build_dir"' EXIT

wait_for_log() {
    local pattern=$1
    local path=$2
    local attempts=0
    while ! grep -q "$pattern" "$path" 2>/dev/null; do
        attempts=$((attempts + 1))
        if ((attempts >= 100)); then
            echo "tooling test failed: browser log never contained $pattern" >&2
            return 1
        fi
        sleep 0.02
    done
}

bash "$script_dir/library/run.sh"
if command -v node >/dev/null 2>&1; then
    node "$script_dir/tooling/background_test.js"
fi

common_flags=(
    -std=c++20
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -pthread
    "-I$repo_root/include"
    "-I$repo_root/tools"
    "-I$repo_root/tools/cfprobs"
)

"${compiler_command[@]}" \
    "${common_flags[@]}" \
    "$script_dir/tooling/core_tests.cpp" \
    "$repo_root/tools/cfprobs/problem.cpp" \
    "$repo_root/tools/cfprobs/workspace.cpp" \
    "$repo_root/tools/cfprobs/bundle.cpp" \
    -o "$build_dir/core-tests"
"$build_dir/core-tests"

tool_sources=()
for source in "$repo_root"/tools/cfprobs/*.cpp; do
    if [[ $(basename "$source") != main.cpp ]]; then
        tool_sources+=("$source")
    fi
done
"${compiler_command[@]}" \
    "${common_flags[@]}" \
    "$script_dir/tooling/tool_tests.cpp" \
    "${tool_sources[@]}" \
    -o "$build_dir/tool-tests"
CFPROBS_BROWSER="$script_dir/tooling/fixtures/browser.sh" \
CFPROBS_TEST_BROWSER_LOG="$build_dir/tool-browser.log" \
CFPROBS_TEST_SUBMISSION_PAYLOAD="$build_dir/tool-submission.json" \
CFPROBS_CHROME_EXTENSION_ID=abcdefghijklmnopabcdefghijklmnop \
    "$build_dir/tool-tests"

sandbox=$build_dir/workspace
mkdir -p "$sandbox/templates" "$sandbox/include"
cp "$repo_root/templates/solution.cpp" "$sandbox/templates/solution.cpp"
cp -R "$repo_root/include/cp" "$sandbox/include/cp"

browser_log=$build_dir/browser.log
editor_log=$build_dir/editor.log
submission_payload=$build_dir/submission.json
clipboard_payload=$build_dir/clipboard.cpp
test_extension_id=abcdefghijklmnopabcdefghijklmnop
export CFPROBS_CHROME_EXTENSION_ID=$test_extension_id
start_output=$(
    CFPROBS_BROWSER="$script_dir/tooling/fixtures/browser.sh" \
    CFPROBS_TEST_BROWSER_LOG="$browser_log" \
    CFPROBS_TEST_PROBLEM_PACKAGE="$script_dir/tooling/fixtures/browser-package.json" \
    CFPROBS_TEST_SUBMISSION_PAYLOAD="$submission_payload" \
    CFPROBS_TEST_EDITOR_LOG="$editor_log" \
    EDITOR="$script_dir/tooling/fixtures/editor.sh" \
        "$repo_root/bin/probs" --root "$sandbox" 99993C
)
grep -q 'Fetched 99993C — Browser bridge problem' <<<"$start_output"
grep -q 'Imported 2 samples' <<<"$start_output"
grep -q 'Opened problems/cf/99993/C/solution.cpp' <<<"$start_output"
bridge_problem_dir=$sandbox/problems/cf/99993/C
test -f "$bridge_problem_dir/problem.json"
test -f "$bridge_problem_dir/samples/01.in"
test -f "$bridge_problem_dir/samples/02.out"
grep -q "$bridge_problem_dir/solution.cpp" "$editor_log"
grep -q '^fetch$' "$browser_log"
grep -qx '99993C' "$sandbox/.build/current-problem"

cp "$script_dir/tooling/fixtures/sum.cpp" "$bridge_problem_dir/solution.cpp"
cp "$script_dir/tooling/fixtures/10.in" "$bridge_problem_dir/samples/01.in"
CFPROBS_BROWSER="$script_dir/tooling/fixtures/browser.sh" \
CFPROBS_TEST_BROWSER_LOG="$browser_log" \
CFPROBS_TEST_PROBLEM_PACKAGE="$script_dir/tooling/fixtures/browser-package.json" \
CFPROBS_TEST_SUBMISSION_PAYLOAD="$submission_payload" \
CFPROBS_TEST_EDITOR_LOG="$editor_log" \
EDITOR="$script_dir/tooling/fixtures/editor.sh" \
    "$repo_root/bin/probs" --root "$sandbox" 99993C >/dev/null
cmp "$script_dir/tooling/fixtures/sum.cpp" "$bridge_problem_dir/solution.cpp"
cmp "$script_dir/tooling/fixtures/02.in" "$bridge_problem_dir/samples/01.in"

submit_output=$(
    cd "$sandbox"
    CFPROBS_BROWSER="$script_dir/tooling/fixtures/browser.sh" \
    CFPROBS_TEST_BROWSER_LOG="$browser_log" \
    CFPROBS_TEST_PROBLEM_PACKAGE="$script_dir/tooling/fixtures/browser-package.json" \
    CFPROBS_TEST_SUBMISSION_PAYLOAD="$submission_payload" \
        "$repo_root/bin/probs" --root "$sandbox" submit
)
grep -q '2/2 tests passed' <<<"$submit_output"
grep -q 'Checked build passed' <<<"$submit_output"
grep -q 'Submitted 99993C as GNU C++20' <<<"$submit_output"
grep -q 'https://codeforces.com/contest/99993/submission/123456789' <<<"$submit_output"
grep -q 'Verdict: TESTING' <<<"$submit_output"
grep -q '"target":"99993C"' "$submission_payload"
grep -q '"language":"GNU C++20"' "$submission_payload"
grep -q '"source":' "$submission_payload"
grep -q '#include' "$submission_payload"
grep -q '^submit$' "$browser_log"

"$repo_root/bin/probs" --root "$sandbox" get 99992A >/dev/null
conflict_problem_dir=$sandbox/problems/cf/99992/A
: >"$browser_log"
if conflict_output=$(
    cd "$conflict_problem_dir"
    CFPROBS_BROWSER="$script_dir/tooling/fixtures/browser.sh" \
    CFPROBS_TEST_BROWSER_LOG="$browser_log" \
        "$repo_root/bin/probs" --root "$sandbox" submit 2>&1
); then
    echo "tooling test failed: submit accepted conflicting current problems" >&2
    exit 1
fi
grep -q 'submission target is ambiguous: current directory is 99992A but current problem is 99993C' \
    <<<"$conflict_output"
test ! -s "$browser_log"

: >"$browser_log"
manual_output=$(
    cd "$conflict_problem_dir"
    CFPROBS_BROWSER="$script_dir/tooling/fixtures/browser.sh" \
    CFPROBS_CLIPBOARD="$script_dir/tooling/fixtures/clipboard.sh" \
    CFPROBS_TEST_BROWSER_LOG="$browser_log" \
    CFPROBS_TEST_CLIPBOARD="$clipboard_payload" \
        "$repo_root/bin/probs" --root "$sandbox" submit --manual 99993C
)
grep -q '2/2 tests passed' <<<"$manual_output"
grep -q 'Checked build passed' <<<"$manual_output"
grep -q 'Copied tested bundle .* to the clipboard' <<<"$manual_output"
grep -q 'Opened Codeforces submission page for 99993C' <<<"$manual_output"
grep -q 'Paste and submit as GNU C++20' <<<"$manual_output"
wait_for_log '^manual$' "$browser_log"
submission_artifact=$(find "$sandbox/.build/submissions" -name '99993C-*.cpp' -print -quit)
cmp "$submission_artifact" "$clipboard_payload"

: >"$browser_log"
fallback_output=$(
    cd "$bridge_problem_dir"
    CFPROBS_BROWSER="$script_dir/tooling/fixtures/browser.sh" \
    CFPROBS_CLIPBOARD="$script_dir/tooling/fixtures/clipboard.sh" \
    CFPROBS_TEST_BROWSER_LOG="$browser_log" \
    CFPROBS_TEST_CLIPBOARD="$clipboard_payload" \
    CFPROBS_TEST_SKIP_CONNECTOR=1 \
        "$repo_root/bin/probs" --root "$sandbox" submit
)
grep -q 'Chrome connector unavailable; using manual submission' <<<"$fallback_output"
grep -q 'Copied tested bundle .* to the clipboard' <<<"$fallback_output"
grep -q '^unavailable$' "$browser_log"
wait_for_log '^manual$' "$browser_log"

: >"$browser_log"
cp "$script_dir/tooling/fixtures/wrong.cpp" "$bridge_problem_dir/solution.cpp"
if (
    cd "$bridge_problem_dir"
    CFPROBS_BROWSER="$script_dir/tooling/fixtures/browser.sh" \
    CFPROBS_TEST_BROWSER_LOG="$browser_log" \
    CFPROBS_TEST_PROBLEM_PACKAGE="$script_dir/tooling/fixtures/browser-package.json" \
    CFPROBS_TEST_SUBMISSION_PAYLOAD="$submission_payload" \
        "$repo_root/bin/probs" --root "$sandbox" submit
) >/dev/null 2>&1; then
    echo "tooling test failed: submit accepted a failing solution" >&2
    exit 1
fi
test ! -s "$browser_log"

printf '99990A\n' >"$sandbox/.build/current-problem"
: >"$browser_log"
if stale_output=$(
    cd "$sandbox"
    CFPROBS_BROWSER="$script_dir/tooling/fixtures/browser.sh" \
    CFPROBS_TEST_BROWSER_LOG="$browser_log" \
        "$repo_root/bin/probs" --root "$sandbox" submit 2>&1
); then
    echo "tooling test failed: submit accepted a stale current problem" >&2
    exit 1
fi
grep -q 'current problem 99990A has no solution' <<<"$stale_output"
test ! -s "$browser_log"

"$repo_root/bin/probs" --root "$sandbox" get 99991A |
    grep -q 'created:'
problem_dir=$sandbox/problems/cf/99991/A
cp "$script_dir/tooling/fixtures/sum.cpp" "$problem_dir/solution.cpp"
cp "$script_dir/tooling/fixtures/02.in" "$problem_dir/samples/02.in"
cp "$script_dir/tooling/fixtures/02.out" "$problem_dir/samples/02.out"
cp "$script_dir/tooling/fixtures/10.in" "$problem_dir/samples/10.in"
cp "$script_dir/tooling/fixtures/10.out" "$problem_dir/samples/10.out"
cp "$script_dir/tooling/fixtures/02.in" "$problem_dir/cases/overflow.in"
cp "$script_dir/tooling/fixtures/02.out" "$problem_dir/cases/overflow.out"
cp "$script_dir/tooling/fixtures/gen.cpp" "$problem_dir/stress/gen.cpp"
cp "$script_dir/tooling/fixtures/brute.cpp" "$problem_dir/stress/brute.cpp"

test_output=$(
    cd "$problem_dir"
    "$repo_root/bin/probs" --root "$sandbox" test --checked
)
grep -q '3/3 passed' <<<"$test_output"
sample_02_line=$(grep -n 'samples/02.in' <<<"$test_output" | cut -d: -f1)
sample_10_line=$(grep -n 'samples/10.in' <<<"$test_output" | cut -d: -f1)
case_line=$(grep -n 'cases/overflow.in' <<<"$test_output" | cut -d: -f1)
if ((sample_02_line >= sample_10_line || sample_10_line >= case_line)); then
    echo "tooling test failed: test cases are not naturally ordered" >&2
    exit 1
fi

cp "$script_dir/tooling/fixtures/02.in" "$problem_dir/cases/incomplete.in"
if (
    cd "$problem_dir"
    "$repo_root/bin/probs" --root "$sandbox" test --checked
) >/dev/null; then
    echo "tooling test failed: incomplete input/output pair was accepted" >&2
    exit 1
fi
rm "$problem_dir/cases/incomplete.in"

cached_output=$(
    cd "$problem_dir"
    "$repo_root/bin/probs" --root "$sandbox" test --checked
)
grep -q 'cached:' <<<"$cached_output"

(
    cd "$problem_dir"
    "$repo_root/bin/probs" --root "$sandbox" bundle
) >"$build_dir/bundled.cpp"
if grep -q '#include "cp/' "$build_dir/bundled.cpp"; then
    echo "tooling test failed: bundle retained a cp include" >&2
    exit 1
fi

(
    cd "$problem_dir"
    "$repo_root/bin/probs" --root "$sandbox" stress -n 5 --seed 11
) | grep -q '5 stress cases passed'

cp "$script_dir/tooling/fixtures/wrong.cpp" "$problem_dir/solution.cpp"
if (
    cd "$problem_dir"
    "$repo_root/bin/probs" --root "$sandbox" stress -n 1 --seed 99
) >/dev/null; then
    echo "tooling test failed: stress mismatch was accepted" >&2
    exit 1
fi
(
    cd "$problem_dir"
    "$repo_root/bin/probs" --root "$sandbox" fail
) | grep -q 'stress-1.in'
test -f "$problem_dir/cases/stress-1.in"
test -f "$problem_dir/cases/stress-1.out"
cp "$script_dir/tooling/fixtures/sum.cpp" "$problem_dir/solution.cpp"
(
    cd "$problem_dir"
    "$repo_root/bin/probs" --root "$sandbox" test
) | grep -q '4/4 passed'

mkdir -p "$sandbox/solutions" "$sandbox/tests/A.71"
cp "$script_dir/tooling/fixtures/sum.cpp" "$sandbox/solutions/A.71.cpp"
cp "$script_dir/tooling/fixtures/02.in" "$sandbox/tests/A.71/case-1.in"
cp "$script_dir/tooling/fixtures/02.out" "$sandbox/tests/A.71/case-1.out"
"$repo_root/bin/probs" --root "$sandbox" test A 71 |
    grep -q '1/1 passed'

port=$((32000 + ($$ % 20000)))
"$repo_root/bin/probs" --root "$sandbox" cc --once --port "$port" \
    >"$build_dir/cc.out" 2>"$build_dir/cc.err" &
cc_pid=$!
if ! curl \
    --silent \
    --fail \
    --retry 20 \
    --retry-connrefused \
    --retry-delay 0 \
    -H 'Content-Type: application/json' \
    --data-binary "@$script_dir/tooling/fixtures/companion.json" \
    "http://127.0.0.1:$port/" \
    >"$build_dir/cc.response"; then
    kill "$cc_pid" 2>/dev/null || true
    wait "$cc_pid" 2>/dev/null || true
    exit 1
fi
wait "$cc_pid"
grep -q 'imported 99992B' "$build_dir/cc.response"
test -f "$sandbox/problems/cf/99992/B/samples/01.in"
test -f "$sandbox/problems/cf/99992/B/samples/01.out"

echo "CLI smoke tests passed"
