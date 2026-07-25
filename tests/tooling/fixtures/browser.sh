#!/usr/bin/env bash

set -euo pipefail

url=$1
if [[ $url != *'#cfx='* ]]; then
    printf '%s\n' manual >>"$CFX_TEST_BROWSER_LOG"
    exit 0
fi
if [[ ${CFX_TEST_SKIP_CONNECTOR:-0} == 1 ]]; then
    printf '%s\n' unavailable >>"$CFX_TEST_BROWSER_LOG"
    exit 0
fi

page_url=${url%%#*}
if [[ $page_url != *cfx_reload=* ]]; then
    echo "browser launch did not force a full navigation" >&2
    exit 1
fi
fragment=${url#*#}
action=${fragment%%&*}
action=${action#cfx=}
port=${fragment#*port=}
port=${port%%&*}
token=${fragment#*token=}
token=${token%%&*}
base=http://127.0.0.1:$port
extension_id=${CFX_CHROME_EXTENSION_ID:?}
origin=chrome-extension://$extension_id

printf '%s\n' "$action" >>"$CFX_TEST_BROWSER_LOG"

status() {
    curl --silent --output /dev/null --write-out '%{http_code}' "$@"
}

expect_status() {
    local expected=$1
    local description=$2
    shift 2
    local actual
    actual=$(status "$@")
    if [[ $actual != "$expected" ]]; then
        echo "$description returned HTTP $actual, expected $expected" >&2
        exit 1
    fi
}

raw_status() {
    local request=$1
    exec 9<>"/dev/tcp/127.0.0.1/$port"
    printf '%b' "$request" >&9
    local protocol result message
    IFS=' ' read -r protocol result message <&9
    exec 9>&- 9<&-
    printf '%s' "$result"
}

valid_preflight() {
    local route=$1
    local method=$2
    local requested=x-cfx-extension
    if [[ $method == POST ]]; then
        requested='content-type, x-cfx-extension'
    fi
    local headers
    headers=$(
        curl \
            --silent \
            --show-error \
            --fail \
            --dump-header - \
            --output /dev/null \
            -X OPTIONS \
            -H "Origin: $origin" \
            -H "Access-Control-Request-Method: $method" \
            -H "Access-Control-Request-Headers: $requested" \
            -H 'Access-Control-Request-Private-Network: true' \
            "$base/$route/$token"
    )
    grep -qi "^Access-Control-Allow-Origin: $origin" <<<"$headers"
    grep -qi '^Access-Control-Allow-Private-Network: true' <<<"$headers"
    grep -qi "^Access-Control-Allow-Methods: $method" <<<"$headers"
    grep -qi '^Access-Control-Allow-Headers: .*X-Cfx-Extension' <<<"$headers"
}

check_preflight() {
    local route=$1
    local method=$2
    local requested=x-cfx-extension
    local wrong_method=POST
    if [[ $method == POST ]]; then
        requested='content-type, x-cfx-extension'
        wrong_method=GET
    fi

    expect_status 403 'foreign-origin preflight' \
        -X OPTIONS \
        -H 'Origin: https://codeforces.com' \
        -H "Access-Control-Request-Method: $method" \
        -H "Access-Control-Request-Headers: $requested" \
        "$base/$route/$token"
    expect_status 403 'preflight without requested method' \
        -X OPTIONS \
        -H "Origin: $origin" \
        -H "Access-Control-Request-Headers: $requested" \
        "$base/$route/$token"
    expect_status 403 'preflight with wrong requested method' \
        -X OPTIONS \
        -H "Origin: $origin" \
        -H "Access-Control-Request-Method: $wrong_method" \
        -H "Access-Control-Request-Headers: $requested" \
        "$base/$route/$token"
    expect_status 403 'preflight without extension header declaration' \
        -X OPTIONS \
        -H "Origin: $origin" \
        -H "Access-Control-Request-Method: $method" \
        -H 'Access-Control-Request-Headers: content-type' \
        "$base/$route/$token"
    valid_preflight "$route" "$method"
}

check_contract() {
    local wrong_mode_route=fetch
    local post_route=result
    if [[ $action == fetch ]]; then
        wrong_mode_route=submission
        post_route=fetch
    fi

    expect_status 403 'request without extension identity' \
        -H 'Sec-Fetch-Site: none' "$base/ready/$token"
    expect_status 403 'extension-origin request without extension identity' \
        -H "Origin: $origin" "$base/ready/$token"
    expect_status 403 'request with wrong extension identity' \
        -H 'X-Cfx-Extension: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' \
        -H 'Sec-Fetch-Site: none' "$base/ready/$token"
    expect_status 403 'cross-site request' \
        -H "X-Cfx-Extension: $extension_id" \
        -H 'Sec-Fetch-Site: cross-site' "$base/ready/$token"
    expect_status 405 'wrong method on active route' \
        -X POST \
        -H "X-Cfx-Extension: $extension_id" \
        -H 'Sec-Fetch-Site: none' "$base/ready/$token"
    expect_status 404 'route from the other bridge mode' \
        -H "X-Cfx-Extension: $extension_id" \
        -H 'Sec-Fetch-Site: none' "$base/$wrong_mode_route/$token"
    expect_status 405 'GET on active POST route' \
        -H "X-Cfx-Extension: $extension_id" \
        -H 'Sec-Fetch-Site: none' "$base/$post_route/$token"

    local host="Host: 127.0.0.1:$port"
    local auth="X-Cfx-Extension: $extension_id"
    local site='Sec-Fetch-Site: none'
    local json='Content-Type: application/json'
    local result
    result=$(raw_status "GET /ready/$token HTTP/1.1\r\n$host\r\n$auth\r\n$site\r\nContent-Length: 1\r\n\r\n")
    [[ $result == 400 ]] || { echo "GET body returned HTTP $result, expected 400" >&2; exit 1; }
    result=$(raw_status "POST /$post_route/$token HTTP/1.1\r\n$host\r\n$auth\r\n$site\r\n$json\r\n\r\n")
    [[ $result == 411 ]] || { echo "missing length returned HTTP $result, expected 411" >&2; exit 1; }
    expect_status 415 'non-JSON POST' \
        -H "X-Cfx-Extension: $extension_id" \
        -H 'Sec-Fetch-Site: none' \
        --data-binary '{}' "$base/$post_route/$token"
    result=$(raw_status "POST /$post_route/$token HTTP/1.1\r\n$host\r\n$auth\r\n$site\r\n$json\r\nContent-Length: 16777217\r\n\r\n")
    [[ $result == 413 ]] || { echo "oversized route body returned HTTP $result, expected 413" >&2; exit 1; }
}

ready() {
    curl \
        --silent \
        --show-error \
        --fail \
        --retry 20 \
        --retry-connrefused \
        --retry-delay 0 \
        -H "X-Cfx-Extension: $extension_id" \
        -H 'Sec-Fetch-Site: none' \
        "$base/ready/$token" \
        >/dev/null
}

if [[ $action == fetch ]]; then
    (
        check_contract
        check_preflight fetch POST
        ready
        curl \
            --silent \
            --show-error \
            --fail \
            --retry 20 \
            --retry-connrefused \
            --retry-delay 0 \
            -H "X-Cfx-Extension: $extension_id" \
            -H 'Sec-Fetch-Site: none' \
            -H 'Content-Type: application/json' \
            --data-binary "@$CFX_TEST_PROBLEM_PACKAGE" \
            "$base/fetch/$token" \
            >/dev/null
    ) &
elif [[ $action == submit ]]; then
    (
        connector_mode=${CFX_TEST_CONNECTOR_MODE:-success}
        if [[ $connector_mode == tle-result ]]; then
            check_contract
            check_preflight submission GET
            valid_preflight result POST
        fi
        ready
        if [[ $connector_mode == ready-only ]]; then
            exit 0
        fi
        if [[ $connector_mode == pre-submit-error ]]; then
            curl \
                --silent \
                --show-error \
                --fail \
                -H "X-Cfx-Extension: $extension_id" \
                -H 'Sec-Fetch-Site: none' \
                -H 'Content-Type: application/json' \
                --data-binary '{"ok":false,"unknown":false,"message":"Codeforces is not signed in in Chrome; sign in, then rerun cfx submit"}' \
                "$base/result/$token" \
                >/dev/null
            exit 0
        fi
        curl \
            --silent \
            --show-error \
            --fail \
            --retry 20 \
            --retry-connrefused \
            --retry-delay 0 \
            -H "X-Cfx-Extension: $extension_id" \
            -H 'Sec-Fetch-Site: none' \
            "$base/submission/$token" \
            >"$CFX_TEST_SUBMISSION_PAYLOAD"
        if [[ $connector_mode == source-only ]]; then
            exit 0
        fi
        expect_status 409 'duplicate submission source request' \
            -H "X-Cfx-Extension: $extension_id" \
            -H 'Sec-Fetch-Site: none' \
            "$base/submission/$token"
        if [[ $connector_mode == unknown-result ]]; then
            result='{"ok":false,"unknown":true,"submissionId":"123456789","message":"response could not be confirmed; check Codeforces before trying again"}'
        else
            submitted_at_millis=$(( $(date +%s) * 1000 - 2000 ))
            result="{\"ok\":true,\"submissionId\":\"123456789\",\"handle\":\"panicsort\",\"submittedAtMillis\":$submitted_at_millis}"
        fi
        curl \
            --silent \
            --show-error \
            --fail \
            -H "X-Cfx-Extension: $extension_id" \
            -H 'Sec-Fetch-Site: none' \
            -H 'Content-Type: application/json' \
            --data-binary "$result" \
            "$base/result/$token" \
            >/dev/null
    ) &
else
    echo "unknown browser action: $action" >&2
    exit 1
fi
