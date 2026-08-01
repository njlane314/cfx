#!/bin/sh
set -eu

script=$0
while [ -L "$script" ]; do
    directory=$(CDPATH= cd -P "$(dirname "$script")" >/dev/null 2>&1 && pwd)
    target=$(readlink "$script")
    case $target in
        /*) script=$target ;;
        *) script=$directory/$target ;;
    esac
done

prefix=$(CDPATH= cd -P "$(dirname "$script")/.." >/dev/null 2>&1 && pwd)
share=$prefix/share/cfx
CFX_SOLUTION_TEMPLATE=${CFX_SOLUTION_TEMPLATE:-$share/solution.cpp}
if [ -z "${CFX_CHROME_EXTENSION_ID:-}" ]; then
    CFX_CHROME_EXTENSION_ID=$(tr -d '[:space:]' <"$share/extension-id")
fi
if [ -n "${CFX_CXX:-}" ]; then
    CXX=$CFX_CXX
elif [ -z "${CXX:-}" ]; then
    if [ -x /opt/homebrew/opt/llvm/bin/clang++ ]; then
        CXX=/opt/homebrew/opt/llvm/bin/clang++
    elif [ -x /usr/local/opt/llvm/bin/clang++ ]; then
        CXX=/usr/local/opt/llvm/bin/clang++
    else
        CXX=c++
    fi
fi
CFX_STD=${CFX_STD:-c++20}
export CFX_SOLUTION_TEMPLATE CFX_CHROME_EXTENSION_ID CFX_STD CXX

exec "$prefix/libexec/cfx" "$@"
