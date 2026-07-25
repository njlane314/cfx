#!/usr/bin/env bash

set -euo pipefail

printf '%s/%s\n' "$PWD" "$1" >"$CFX_TEST_EDITOR_LOG"
