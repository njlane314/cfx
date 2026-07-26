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
CFX_ASSET_ROOT=${CFX_ASSET_ROOT:-$prefix/share/cfx}
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
export CFX_ASSET_ROOT CFX_STD CXX

exec "$prefix/libexec/cfx" "$@"
