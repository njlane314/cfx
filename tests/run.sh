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
trap 'rm -rf "$build_dir"' EXIT

bash "$script_dir/library/run.sh"

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
"$build_dir/tool-tests"

sandbox=$build_dir/workspace
mkdir -p "$sandbox/templates" "$sandbox/include"
cp "$repo_root/templates/solution.cpp" "$sandbox/templates/solution.cpp"
cp -R "$repo_root/include/cp" "$sandbox/include/cp"

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
