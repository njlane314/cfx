#!/usr/bin/env bash

set -euo pipefail

printf '%s/%s\n' "$PWD" "$1" >"$CFPROBS_TEST_EDITOR_LOG"
