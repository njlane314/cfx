#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

bash "$script_dir/library/run.sh"
bash "$script_dir/tooling/run.sh"
bash "$script_dir/workflows/fetch-submit.sh"
bash "$script_dir/workflows/contest-sync.sh"
bash "$script_dir/workflows/judge-stress.sh"
bash "$script_dir/workflows/companion.sh"

echo 'all tests passed'
