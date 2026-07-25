#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
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
    share/cfx/include/cp/prelude.hpp share/cfx/browser/extension-id LICENSE; do
    [[ -e $work/$package/$path ]] || { echo "release test: package lacks $path" >&2; exit 1; }
done
"$work/$package/bin/cfx" --help >/dev/null

workspace=$work/workspace
mkdir -p "$workspace/problems/cf/4/A/cases"
printf '8\n' >"$workspace/problems/cf/4/A/cases/case-1.in"
printf 'YES\n' >"$workspace/problems/cf/4/A/cases/case-1.out"
cat >"$workspace/problems/cf/4/A/solution.cpp" <<'CPP'
#include "cp/prelude.hpp"
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
cmp -s "$work/head.rb" "$root/Formula/cfx.rb"

demo=$work/dist/cfx-0.0.0-test-demo-20s.mp4
printf 'release demo fixture\n' >"$demo"
"$script_dir/checksums.sh" "$archive" "$demo" >"$work/SHA256SUMS"
grep -q '  cfx-0.0.0-test-demo-20s.mp4$' "$work/SHA256SUMS"
(
    cd "$(dirname "$archive")"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum -c "$work/SHA256SUMS" >/dev/null
    else
        shasum -a 256 -c "$work/SHA256SUMS" >/dev/null
    fi
)

echo "release packaging: passed"
