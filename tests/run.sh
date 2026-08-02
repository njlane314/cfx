#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

bash "$script_dir/tooling/run.sh"
bash "$script_dir/workflows/pick.sh"
bash "$script_dir/workflows/fetch-submit.sh"
bash "$script_dir/workflows/judge-stress.sh"

echo 'all tests passed'
