#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$script_dir/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/cfx-release-test.XXXXXX")
trap 'rm -rf "$work"' EXIT

make -s -C "$root" build
archive=$("$script_dir/package.sh" 0.0.0-test "$(uname -s | tr '[:upper:]' '[:lower:]' | sed 's/darwin/macos/')" \
    "$(uname -m | sed 's/aarch64/arm64/')" "$root/.build/tools/cfx" "$work/dist")
[[ -f $archive ]]

tar -xzf "$archive" -C "$work"
package=${archive##*/}
package=${package%.tar.gz}
for path in bin/cfx libexec/cfx share/cfx/templates/solution.cpp \
    share/cfx/browser/extension-id \
    share/man/man1/cfx.1 LICENSE; do
    [[ -e $work/$package/$path ]] || { echo "release test: package lacks $path" >&2; exit 1; }
done
for header in types utility contract debug disjoint_set fenwick_tree segment_tree modint kmp; do
    path=$work/$package/share/cfx/include/cp/$header
    [[ -f $path ]] || { echo "release test: package lacks include/cp/$header" >&2; exit 1; }
done
legacy=$(find "$work/$package/share/cfx/include/cp" -type f -name '*.hpp' -print -quit)
[[ -z $legacy ]] || { echo "release test: package contains legacy header $legacy" >&2; exit 1; }
grep -q '^\.TH CFX 1 ' "$work/$package/share/man/man1/cfx.1"
! grep -q '\\-\\-remote-check' "$work/$package/share/man/man1/cfx.1"
"$work/$package/bin/cfx" --help >/dev/null
! "$work/$package/bin/cfx" help submit | grep -q -- '--remote-check'

workspace=$work/workspace
export CFX_STATE_ROOT=$work/state
mkdir -p "$workspace/codeforces/4/A/cases"
printf '8\n' >"$workspace/codeforces/4/A/cases/case-1.in"
printf 'YES\n' >"$workspace/codeforces/4/A/cases/case-1.out"
cat >"$workspace/codeforces/4/A/solution.cpp" <<'CPP'
#include <cp/types>
#include <iostream>

int main() {
    cp::i64 weight = 0;
    std::cin >> weight;
    std::cout << (weight > 2 && weight % 2 == 0 ? "YES" : "NO") << '\n';
}
CPP
(
    cd "$workspace"
    output=$("$work/$package/bin/cfx" test 4A)
    [[ $output == *'1/1 passed'* ]]
)

source_archive=$("$script_dir/source-archive.sh" 0.0.0-test "$work/source")
[[ -f $source_archive ]]
[[ $(tar -tzf "$source_archive" | sed -n '1p') == cfx-0.0.0-test/* ]]

"$script_dir/render-formula.sh" --head >"$work/head.rb"
"$script_dir/render-formula.sh" 1.2.3 \
    0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef \
    >"$work/stable.rb"
ruby -c "$work/head.rb" >/dev/null
ruby -c "$work/stable.rb" >/dev/null
cmp -s "$work/head.rb" "$root/release/cfx.rb"

"$script_dir/checksums.sh" "$archive" >"$work/SHA256SUMS"
(
    cd "$(dirname "$archive")"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum -c "$work/SHA256SUMS" >/dev/null
    else
        shasum -a 256 -c "$work/SHA256SUMS" >/dev/null
    fi
)

echo "release packaging: passed"
