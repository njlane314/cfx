#!/usr/bin/env bash

set -euo pipefail

url=$1
page_url=${url%%#*}
if [[ $page_url != *cfprobs_reload=* ]]; then
    echo "browser launch did not force a full navigation" >&2
    exit 1
fi
fragment=${url#*#}
action=${fragment%%&*}
action=${action#cfprobs=}
port=${fragment#*port=}
port=${port%%&*}
token=${fragment#*token=}
token=${token%%&*}
base=http://127.0.0.1:$port

printf '%s\n' "$action" >>"$CFPROBS_TEST_BROWSER_LOG"

preflight_route=fetch
if [[ $action == submit ]]; then
    preflight_route=submission
fi
preflight() {
    local headers
    headers=$(
        curl \
            --silent \
            --show-error \
            --fail \
            --dump-header - \
            --output /dev/null \
            -X OPTIONS \
            -H 'Origin: https://codeforces.com' \
            -H 'Access-Control-Request-Private-Network: true' \
            "$base/$preflight_route/$token"
    )
    grep -qi '^Access-Control-Allow-Origin: https://codeforces.com' <<<"$headers"
    grep -qi '^Access-Control-Allow-Private-Network: true' <<<"$headers"
    grep -qi '^Access-Control-Allow-Methods: .*OPTIONS' <<<"$headers"
}

if [[ $action == fetch ]]; then
    (
        preflight
        curl \
            --silent \
            --show-error \
            --fail \
            --retry 20 \
            --retry-connrefused \
            --retry-delay 0 \
            -H 'Origin: https://codeforces.com' \
            -H 'Content-Type: text/plain;charset=UTF-8' \
            --data-binary "@$CFPROBS_TEST_PROBLEM_PACKAGE" \
            "$base/fetch/$token" \
            >/dev/null
    ) &
elif [[ $action == submit ]]; then
    (
        preflight
        curl \
            --silent \
            --show-error \
            --fail \
            --retry 20 \
            --retry-connrefused \
            --retry-delay 0 \
            -H 'Origin: https://codeforces.com' \
            "$base/submission/$token" \
            >"$CFPROBS_TEST_SUBMISSION_PAYLOAD"
        duplicate_status=$(
            curl \
                --silent \
                --output /dev/null \
                --write-out '%{http_code}' \
                -H 'Origin: https://codeforces.com' \
                "$base/submission/$token"
        )
        if [[ $duplicate_status != 409 ]]; then
            echo "duplicate submission source request returned HTTP $duplicate_status" >&2
            exit 1
        fi
        curl \
            --silent \
            --show-error \
            --fail \
            -H 'Origin: https://codeforces.com' \
            -H 'Content-Type: text/plain;charset=UTF-8' \
            --data-binary \
            '{"ok":true,"url":"https://codeforces.com/contest/99993/submission/123456789","verdict":"TESTING","message":"submission created"}' \
            "$base/result/$token" \
            >/dev/null
    ) &
else
    echo "unknown browser action: $action" >&2
    exit 1
fi
