#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
fixtures=$repo_root/tests/tooling/fixtures
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/cfx-judge-workflow.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT

sandbox=$build_dir/workspace
state=$build_dir/state
export CFX_STATE_ROOT=$state
mkdir -p "$sandbox"

for removed_alias in new run rerun get cc bundle stress fail; do
    if alias_output=$(
        "$repo_root/cfx" --root "$sandbox" "$removed_alias" 2>&1
    ); then
        echo "judge workflow: removed alias '$removed_alias' was accepted" >&2
        exit 1
    else
        alias_status=$?
    fi
    [[ $alias_status == 2 ]]
    grep -q 'cannot parse problem' <<<"$alias_output"
    test ! -e "$sandbox/codeforces"
    test ! -e "$sandbox/.cfx"
done

if "$repo_root/cfx" --root "$sandbox" A 99991 >/dev/null 2>&1; then
    echo "judge workflow: two-token problem ID was accepted" >&2
    exit 1
fi

problem_dir=$sandbox/codeforces/99991/A
sample_dir=$state/codeforces/99991/A/samples
mkdir -p "$problem_dir/cases" "$sample_dir"
cp "$repo_root/solution.cpp" "$problem_dir/solution.cpp"
cmp "$repo_root/solution.cpp" "$problem_dir/solution.cpp"

if alias_output=$(
    cd "$problem_dir"
    "$repo_root/cfx" --root "$sandbox" test --check 2>&1
); then
    echo "judge workflow: test accepted removed --check alias" >&2
    exit 1
fi
grep -q 'test: unknown option --check' <<<"$alias_output"

cp "$fixtures/sum.cpp" "$problem_dir/solution.cpp"
cp "$fixtures/02.in" "$sample_dir/02.in"
cp "$fixtures/02.out" "$sample_dir/02.out"
cp "$fixtures/10.in" "$sample_dir/10.in"
cp "$fixtures/10.out" "$sample_dir/10.out"
cp "$fixtures/02.in" "$problem_dir/cases/overflow.in"
cp "$fixtures/02.out" "$problem_dir/cases/overflow.out"

test_output=$(
    cd "$problem_dir"
    "$repo_root/cfx" --root "$sandbox" test --checked
)
grep -q '3/3 passed' <<<"$test_output"
grep -q 'limits: time 5.000s (fallback), memory unlimited, output 64.0MiB' <<<"$test_output"
grep -q 'CPU, .* wall' <<<"$test_output"
sample_02_line=$(grep -n 'samples/02.in' <<<"$test_output" | cut -d: -f1)
sample_10_line=$(grep -n 'samples/10.in' <<<"$test_output" | cut -d: -f1)
case_line=$(grep -n 'cases/overflow.in' <<<"$test_output" | cut -d: -f1)
if ((sample_02_line >= sample_10_line || sample_10_line >= case_line)); then
    echo "judge workflow: test cases are not naturally ordered" >&2
    exit 1
fi

if tiny_limit_output=$(
    cd "$problem_dir"
    "$repo_root/cfx" --root "$sandbox" test --time-limit 0.0001 2>&1
); then
    echo "judge workflow: sub-millisecond time limit was accepted" >&2
    exit 1
fi
grep -q -- '--time-limit must be at least 0.001 seconds' <<<"$tiny_limit_output"

cp "$fixtures/02.in" "$problem_dir/cases/incomplete.in"
if (
    cd "$problem_dir"
    "$repo_root/cfx" --root "$sandbox" test --checked
) >/dev/null; then
    echo "judge workflow: incomplete input/output pair was accepted" >&2
    exit 1
fi
rm "$problem_dir/cases/incomplete.in"

test ! -e "$sandbox/.build"
test ! -e "$sandbox/.cfx"

echo "judge workflow passed"
