#!/usr/bin/env bash

set -euo pipefail

: "${CFPROBS_TEST_CLIPBOARD:?}"
temporary=$CFPROBS_TEST_CLIPBOARD.tmp.$$
trap 'rm -f "$temporary"' EXIT HUP INT TERM
dd of="$temporary" 2>/dev/null
chmod 600 "$temporary"
mv -f "$temporary" "$CFPROBS_TEST_CLIPBOARD"
trap - EXIT HUP INT TERM
