#!/usr/bin/env bash

set -euo pipefail

url=$1
if [[ $url != *'#cfprobs='* ]]; then
    printf '%s\n' manual >>"$CFPROBS_TEST_BROWSER_LOG"
    exit 0
fi
if [[ ${CFPROBS_TEST_SKIP_CONNECTOR:-0} == 1 ]]; then
    printf '%s\n' unavailable >>"$CFPROBS_TEST_BROWSER_LOG"
    exit 0
fi

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
extension_id=${CFPROBS_CHROME_EXTENSION_ID:?}
origin=chrome-extension://$extension_id

printf '%s\n' "$action" >>"$CFPROBS_TEST_BROWSER_LOG"

preflight_route=fetch
if [[ $action == submit ]]; then
    preflight_route=submission
fi
preflight() {
    local headers
    local rejected_headers
    local rejected_status
    rejected_headers=$(mktemp "${TMPDIR:-/tmp}/cfprobs-rejected-origin.XXXXXX")
    if ! rejected_status=$(
        curl \
            --silent \
            --output /dev/null \
            --dump-header "$rejected_headers" \
            --write-out '%{http_code}' \
            -X OPTIONS \
            -H 'Origin: https://codeforces.com' \
            "$base/$preflight_route/$token"
    ); then
        rm -f "$rejected_headers"
        return 1
    fi
    if [[ $rejected_status != 403 ]] ||
        grep -qi '^Access-Control-Allow-Origin:' "$rejected_headers"; then
        rm -f "$rejected_headers"
        echo "browser bridge accepted the Codeforces page origin" >&2
        exit 1
    fi
    rm -f "$rejected_headers"

    rejected_status=$(
        curl \
            --silent \
            --output /dev/null \
            --write-out '%{http_code}' \
            -H "X-Cfprobs-Extension: $extension_id" \
            -H 'Sec-Fetch-Site: cross-site' \
            "$base/$preflight_route/$token"
    )
    if [[ $rejected_status != 403 ]]; then
        echo "browser bridge accepted a cross-site request" >&2
        exit 1
    fi

    headers=$(
        curl \
            --silent \
            --show-error \
            --fail \
            --dump-header - \
            --output /dev/null \
            -X OPTIONS \
            -H "Origin: $origin" \
            -H 'Access-Control-Request-Headers: x-cfprobs-extension' \
            -H 'Access-Control-Request-Private-Network: true' \
            "$base/$preflight_route/$token"
    )
    grep -qi "^Access-Control-Allow-Origin: $origin" <<<"$headers"
    grep -qi '^Access-Control-Allow-Private-Network: true' <<<"$headers"
    grep -qi '^Access-Control-Allow-Methods: .*OPTIONS' <<<"$headers"
    grep -qi '^Access-Control-Allow-Headers: .*X-Cfprobs-Extension' <<<"$headers"
}

ready() {
    curl \
        --silent \
        --show-error \
        --fail \
        --retry 20 \
        --retry-connrefused \
        --retry-delay 0 \
        -H 'Sec-Fetch-Site: none' \
        "$base/ready/$token" \
        >/dev/null
}

if [[ $action == fetch ]]; then
    (
        ready
        preflight
        curl \
            --silent \
            --show-error \
            --fail \
            --retry 20 \
            --retry-connrefused \
            --retry-delay 0 \
            -H "X-Cfprobs-Extension: $extension_id" \
            -H 'Sec-Fetch-Site: none' \
            -H 'Content-Type: text/plain;charset=UTF-8' \
            --data-binary "@$CFPROBS_TEST_PROBLEM_PACKAGE" \
            "$base/fetch/$token" \
            >/dev/null
    ) &
elif [[ $action == submit ]]; then
    (
        ready
        connector_mode=${CFPROBS_TEST_CONNECTOR_MODE:-success}
        if [[ $connector_mode == ready-only ]]; then
            exit 0
        fi
        if [[ $connector_mode == pre-submit-error ]]; then
            curl \
                --silent \
                --show-error \
                --fail \
                -H "X-Cfprobs-Extension: $extension_id" \
                -H 'Sec-Fetch-Site: none' \
                -H 'Content-Type: text/plain;charset=UTF-8' \
                --data-binary '{"ok":false,"unknown":false,"url":"https://codeforces.com/enter","verdict":"","message":"Codeforces is not signed in in Chrome; sign in, then rerun probs submit"}' \
                "$base/result/$token" \
                >/dev/null
            exit 0
        fi
        preflight
        curl \
            --silent \
            --show-error \
            --fail \
            --retry 20 \
            --retry-connrefused \
            --retry-delay 0 \
            -H "X-Cfprobs-Extension: $extension_id" \
            -H 'Sec-Fetch-Site: none' \
            "$base/submission/$token" \
            >"$CFPROBS_TEST_SUBMISSION_PAYLOAD"
        if [[ $connector_mode == source-only ]]; then
            exit 0
        fi
        duplicate_status=$(
            curl \
                --silent \
                --output /dev/null \
                --write-out '%{http_code}' \
                -H "X-Cfprobs-Extension: $extension_id" \
                -H 'Sec-Fetch-Site: none' \
                "$base/submission/$token"
        )
        if [[ $duplicate_status != 409 ]]; then
            echo "duplicate submission source request returned HTTP $duplicate_status" >&2
            exit 1
        fi
        if [[ $connector_mode == unknown-result ]]; then
            result='{"ok":false,"unknown":true,"url":"https://codeforces.com/contest/99993/submit","verdict":"","message":"response could not be confirmed; check Codeforces before trying again"}'
        else
            result='{"ok":true,"url":"https://codeforces.com/contest/99993/submission/123456789","verdict":"TESTING","message":"submission created"}'
        fi
        curl \
            --silent \
            --show-error \
            --fail \
            -H "X-Cfprobs-Extension: $extension_id" \
            -H 'Sec-Fetch-Site: none' \
            -H 'Content-Type: text/plain;charset=UTF-8' \
            --data-binary "$result" \
            "$base/result/$token" \
            >/dev/null
    ) &
else
    echo "unknown browser action: $action" >&2
    exit 1
fi
