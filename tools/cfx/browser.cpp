#include "browser.hpp"

#include "browser_http.hpp"
#include "codeforces.hpp"
#include "json.hpp"
#include "process.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

namespace cfx {
namespace {

using browser_http::Error;
using browser_http::Request;
using browser_http::RequestHead;
using browser_http::Response;

std::string system_error(std::string_view operation) {
    return std::string(operation) + ": " + std::strerror(errno);
}

std::string random_hex(std::size_t byte_count) {
    const int descriptor = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        throw std::runtime_error(system_error("cannot open system random source"));
    }

    std::vector<unsigned char> bytes(byte_count);
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = ::read(descriptor, bytes.data() + offset, bytes.size() - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            const int error = errno;
            (void)::close(descriptor);
            errno = error;
            throw std::runtime_error(system_error("cannot read system random source"));
        }
    }
    (void)::close(descriptor);

    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const unsigned char byte : bytes) {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0fU]);
    }
    std::fill(bytes.begin(), bytes.end(), 0);
    return result;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string trim(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

bool constant_time_equal(std::string_view left, std::string_view right) {
    const std::size_t length = std::max(left.size(), right.size());
    std::size_t difference = left.size() ^ right.size();
    for (std::size_t index = 0; index < length; ++index) {
        const unsigned char left_byte =
            index < left.size() ? static_cast<unsigned char>(left[index]) : 0;
        const unsigned char right_byte =
            index < right.size() ? static_cast<unsigned char>(right[index]) : 0;
        difference |= static_cast<std::size_t>(left_byte ^ right_byte);
    }
    return difference == 0;
}

bool extension_identifier(std::string_view value) {
    return value.size() == 32 &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return character >= 'a' && character <= 'p';
           });
}

std::string configured_extension_identifier() {
    if (const char* configured = std::getenv("CFX_CHROME_EXTENSION_ID");
        configured != nullptr && *configured != '\0') {
        return configured;
    }
    const char* root = std::getenv("CFX_ROOT");
    if (root == nullptr || *root == '\0') {
        return {};
    }
    std::ifstream input(std::filesystem::path(root) / "browser" / "extension-id");
    if (!input) {
        return {};
    }
    std::string value{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(),
    };
    return trim(value);
}

bool allowed_origin(std::string_view origin, std::string_view expected_identifier) {
    constexpr std::string_view prefix = "chrome-extension://";
    if (!origin.starts_with(prefix)) {
        return false;
    }
    const std::string_view identifier = origin.substr(prefix.size());
    return extension_identifier(identifier) && identifier == expected_identifier;
}

void require_host(const RequestHead& request, std::uint16_t port) {
    const std::string expected = "127.0.0.1:" + std::to_string(port);
    if (browser_http::header(request, "host") != expected) {
        throw Error(403, "invalid host");
    }
}

std::string optional_string(const Json& document, std::string_view name) {
    const Json* value = document.find(name);
    if (value == nullptr) {
        return {};
    }
    if (!value->is_string()) {
        throw Error(400, std::string(name) + " must be a string");
    }
    return value->string();
}

bool required_bool(const Json& document, std::string_view name) {
    const Json* value = document.find(name);
    if (value == nullptr || !value->is_bool()) {
        throw Error(400, std::string(name) + " must be a boolean");
    }
    return value->boolean();
}

bool optional_bool(const Json& document, std::string_view name) {
    const Json* value = document.find(name);
    if (value == nullptr) {
        return false;
    }
    if (!value->is_bool()) {
        throw Error(400, std::string(name) + " must be a boolean");
    }
    return value->boolean();
}

std::uint64_t nonnegative_integer(const Json& value, std::string_view name) {
    if (!value.is_number()) {
        throw Error(400, std::string(name) + " must be a nonnegative integer");
    }
    const double number = value.number();
    constexpr double maximum_safe_integer = 9007199254740991.0;
    if (!std::isfinite(number) || number < 0.0 || std::floor(number) != number ||
        number > maximum_safe_integer) {
        throw Error(400, std::string(name) + " must be a nonnegative integer");
    }
    return static_cast<std::uint64_t>(number);
}

std::string contest_id(std::string_view target) {
    const std::size_t length = target.find_first_not_of("0123456789");
    if (length == 0 || length == std::string_view::npos) {
        throw std::invalid_argument("browser submission target is invalid");
    }
    return std::string(target.substr(0, length));
}

bool valid_submission_id(std::string_view value) {
    if (value.empty() ||
        !std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return std::isdigit(character) != 0;
        })) {
        return false;
    }
    std::uint64_t number = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
    return error == std::errc() && end == value.data() + value.size() && number > 0 &&
           number <= 9007199254740991ULL;
}

bool valid_handle(std::string_view value) {
    return !value.empty() && value.size() <= 64 &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isalnum(character) != 0 || character == '_' || character == '-' ||
                      character == '.';
           });
}

struct BrowserSubmissionIdentity {
    std::string id;
    std::string handle;
    std::uint64_t submitted_at_millis = 0;
};

struct ParsedSubmitResult {
    BrowserSubmissionIdentity identity;
    std::string error;
    bool unknown = false;
};

ParsedSubmitResult parse_submit_result(std::string_view body) {
    Json document;
    try {
        document = parse_json(body);
    } catch (const JsonError&) {
        throw Error(400, "result is not valid JSON");
    }
    if (!document.is_object()) {
        throw Error(400, "result must be a JSON object");
    }

    const bool ok = required_bool(document, "ok");
    ParsedSubmitResult result;
    result.identity.id = optional_string(document, "submissionId");
    result.identity.handle = optional_string(document, "handle");
    result.error = optional_string(document, "error");
    result.unknown = optional_bool(document, "unknown");
    if (result.error.empty()) {
        result.error = optional_string(document, "message");
    }
    if (!result.identity.id.empty() && !valid_submission_id(result.identity.id)) {
        throw Error(400, "result has an invalid submission ID");
    }
    if (!result.identity.handle.empty() && !valid_handle(result.identity.handle)) {
        throw Error(400, "result has an invalid Codeforces handle");
    }
    if (const Json* submitted = document.find("submittedAtMillis"); submitted != nullptr) {
        result.identity.submitted_at_millis = nonnegative_integer(*submitted, "submittedAtMillis");
    }
    if (ok && result.unknown) {
        throw Error(400, "successful result cannot have unknown status");
    }
    if (ok && (result.identity.id.empty() || result.identity.handle.empty())) {
        throw Error(400, "successful result has no confirmed submission identity");
    }
    if (ok) {
        result.error.clear();
    } else if (result.error.empty()) {
        result.error = "Codeforces rejected the submission";
    }
    return result;
}

std::string result_error(std::string_view body) {
    try {
        const Json document = parse_json(body);
        if (!document.is_object()) {
            return "browser connector failed";
        }
        const std::string message = optional_string(document, "message");
        return message.empty() ? "browser connector failed" : message;
    } catch (const std::exception&) {
        return "browser connector failed";
    }
}

std::string submission_response(const BrowserSubmitRequest& request) {
    return "{\"target\":" + json_quote(request.target) + ",\"index\":" +
           json_quote(request.index) + ",\"language\":" + json_quote(request.language) +
           ",\"source\":" + json_quote(request.source) + "}";
}

struct BrowserCommand {
    std::vector<std::string> arguments;
    bool detached = false;
};

BrowserCommand browser_command(std::string_view url) {
    const char* configured = std::getenv("CFX_BROWSER");
    if (configured != nullptr && *configured != '\0') {
        std::vector<std::string> command = split_command_words(configured);
        if (command.empty()) {
            throw std::runtime_error("CFX_BROWSER is empty");
        }
        bool replaced = false;
        for (std::string& argument : command) {
            if (argument == "{url}") {
                argument = url;
                replaced = true;
            }
        }
        if (!replaced) {
            command.emplace_back(url);
        }
        return {std::move(command), true};
    }
#ifdef __APPLE__
    return {{"open", "-a", "Google Chrome", std::string(url)}, false};
#else
    return {{"google-chrome", std::string(url)}, true};
#endif
}

enum class BridgeMode { fetch, submit };
enum class Route { ready, submission, result, fetch, fetch_error };

std::string_view route_name(Route route) {
    switch (route) {
    case Route::ready:
        return "ready";
    case Route::submission:
        return "submission";
    case Route::result:
        return "result";
    case Route::fetch:
        return "fetch";
    case Route::fetch_error:
        return "fetch-error";
    }
    return {};
}

std::string_view route_method(Route route) {
    return route == Route::ready || route == Route::submission ? "GET" : "POST";
}

bool active_route(BridgeMode mode, Route route) {
    if (route == Route::ready) {
        return true;
    }
    return mode == BridgeMode::submit
               ? route == Route::submission || route == Route::result
               : route == Route::fetch || route == Route::fetch_error;
}

std::set<std::string> requested_headers(const RequestHead& request) {
    const std::string value = browser_http::header(request, "access-control-request-headers");
    std::set<std::string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t comma = value.find(',', start);
        const std::string name = lower(trim(std::string_view(value).substr(
            start, comma == std::string::npos ? std::string::npos : comma - start)));
        if (name.empty() || !result.insert(name).second) {
            throw Error(403, "invalid preflight request headers");
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return result;
}

bool json_content_type(const RequestHead& request) {
    const std::string value = lower(trim(browser_http::header(request, "content-type")));
    return value == "application/json" || value == "application/json;charset=utf-8" ||
           value == "application/json; charset=utf-8";
}

Response response(int status, std::string body, std::string_view origin = {},
                  std::string_view preflight_method = {}) {
    Response result{status,
                    {
                        {"Cache-Control", "no-store"},
                        {"Content-Type", "application/json; charset=utf-8"},
                        {"Referrer-Policy", "no-referrer"},
                        {"X-Content-Type-Options", "nosniff"},
                    },
                    std::move(body)};
    if (!origin.empty()) {
        result.headers.emplace("Access-Control-Allow-Origin", origin);
        result.headers.emplace("Access-Control-Allow-Private-Network", "true");
        result.headers.emplace("Vary", "Origin");
    }
    if (!preflight_method.empty()) {
        result.headers.emplace("Access-Control-Allow-Methods", preflight_method);
        result.headers.emplace("Access-Control-Allow-Headers",
                               preflight_method == "POST"
                                   ? "Content-Type, X-Cfx-Extension"
                                   : "X-Cfx-Extension");
        result.headers.emplace("Access-Control-Max-Age", "60");
    }
    return result;
}

struct RequestPolicy {
    Route route;
    bool preflight = false;
    std::string origin;
    std::size_t body_limit = 0;
};

struct BridgeCompletion {
    std::string fetched_package;
    BrowserSubmissionIdentity identity;
    std::string error;
    bool submission_unknown = false;
    bool complete = false;
};

class Bridge {
  public:
    Bridge(BridgeMode mode, std::string page_url, BrowserSubmitRequest request,
           BrowserBridgeOptions options)
        : mode_(mode), page_url_(std::move(page_url)), request_(std::move(request)),
          options_(std::move(options)), token_(random_hex(32)), navigation_nonce_(random_hex(8)) {
        if (options_.extension_id.empty()) {
            options_.extension_id = configured_extension_identifier();
        }
        validate();
    }

    ~Bridge() {
        std::fill(token_.begin(), token_.end(), '\0');
        std::fill(navigation_nonce_.begin(), navigation_nonce_.end(), '\0');
        std::fill(request_.source.begin(), request_.source.end(), '\0');
    }

    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;

    void open_browser() const {
        open_browser_url(launch_url());
    }

    BridgeCompletion wait(const std::chrono::steady_clock::time_point& started,
                          const std::chrono::steady_clock::time_point& deadline) {
        BridgeCompletion completion;
        std::size_t connections = 0;
        const auto connect_deadline = std::min(deadline, started + options_.connect_timeout);

        while (!completion.complete) {
            if (connections >= options_.max_connections) {
                throw std::runtime_error("browser connector sent too many requests");
            }
            const auto now = std::chrono::steady_clock::now();
            if (!connector_ready_ && now >= connect_deadline) {
                throw BrowserConnectorUnavailable(
                    "Chrome connector did not respond; install it or use cfx submit --manual");
            }
            if (now >= deadline) {
                if (mode_ == BridgeMode::submit) {
                    if (submission_served_) {
                        throw std::runtime_error(
                            "submission status is unknown; check Chrome before trying again");
                    }
                    throw BrowserConnectorUnavailable(
                        "Chrome connector stopped before requesting the submission");
                }
                throw std::runtime_error("Chrome connector timed out");
            }

            const auto phase_deadline = connector_ready_ ? deadline : connect_deadline;
            std::optional<browser_http::Connection> accepted =
                server_.accept_until(phase_deadline, options_.request_timeout);
            if (!accepted) {
                continue;
            }
            ++connections;
            browser_http::Connection connection = std::move(*accepted);
            std::string response_origin;
            try {
                std::optional<RequestPolicy> policy;
                const auto request_deadline =
                    std::min(deadline, std::chrono::steady_clock::now() + options_.request_timeout);
                const Request incoming = connection.read(request_deadline, [&](const RequestHead& head) {
                    policy = request_policy(head, response_origin);
                    return policy->body_limit;
                });
                connection.send(handle(incoming, *policy, completion));
            } catch (const Error& error) {
                connection.send(response(error.status(),
                                         "{\"error\":" + json_quote(error.what()) + "}",
                                         response_origin));
            } catch (const std::exception&) {
                connection.send(response(400, "{\"error\":\"invalid request\"}",
                                         response_origin));
            }
        }
        return completion;
    }

  private:
    void validate() const {
        if (page_url_.empty()) {
            throw std::invalid_argument("browser page URL is empty");
        }
        if (mode_ == BridgeMode::submit &&
            (request_.target.empty() || request_.index.empty() || request_.language.empty() ||
             request_.source.empty())) {
            throw std::invalid_argument("browser submission is incomplete");
        }
        if (request_.source.size() > options_.max_source_bytes) {
            throw std::invalid_argument("browser submission source is too large");
        }
        if (options_.request_timeout <= std::chrono::milliseconds::zero() ||
            options_.connect_timeout <= std::chrono::milliseconds::zero() ||
            options_.wait_timeout <= std::chrono::milliseconds::zero() ||
            options_.verdict_poll_interval <= std::chrono::milliseconds::zero() ||
            options_.max_fetch_bytes == 0 || options_.max_result_bytes == 0 ||
            options_.max_connections == 0) {
            throw std::invalid_argument("browser connector limits must be positive");
        }
        if (!options_.extension_id.empty() && !extension_identifier(options_.extension_id)) {
            throw std::invalid_argument(
                "CFX_CHROME_EXTENSION_ID must be 32 lowercase letters from a to p");
        }
        if (options_.extension_id.empty()) {
            throw std::runtime_error(
                "Chrome extension ID is not configured; browser/extension-id is missing");
        }
    }

    [[nodiscard]] std::string launch_url() const {
        std::string url = page_url_.substr(0, page_url_.find('#'));
        url += url.find('?') == std::string::npos ? '?' : '&';
        url += "cfx_reload=" + navigation_nonce_;
        url += "#cfx=";
        url += mode_ == BridgeMode::fetch ? "fetch" : "submit";
        url += "&port=" + std::to_string(server_.port()) + "&token=" + token_;
        return url;
    }

    [[nodiscard]] std::optional<Route> find_route(std::string_view target) const {
        constexpr std::array routes{Route::ready, Route::submission, Route::result, Route::fetch,
                                    Route::fetch_error};
        for (const Route route : routes) {
            if (constant_time_equal(target,
                                    "/" + std::string(route_name(route)) + "/" + token_)) {
                return route;
            }
        }
        return std::nullopt;
    }

    std::string require_actual_client(const RequestHead& request) const {
        const std::string claimed = browser_http::header(request, "x-cfx-extension");
        if (!constant_time_equal(claimed, options_.extension_id)) {
            throw Error(403, "extension identity is not allowed");
        }
        const std::string origin = browser_http::header(request, "origin");
        if (!origin.empty()) {
            if (!allowed_origin(origin, options_.extension_id)) {
                throw Error(403, "origin is not allowed");
            }
        } else if (lower(browser_http::header(request, "sec-fetch-site")) != "none") {
            throw Error(403, "origin is not allowed");
        }
        return origin;
    }

    std::string require_preflight(const RequestHead& request, Route route) const {
        const std::string origin = browser_http::header(request, "origin");
        if (!allowed_origin(origin, options_.extension_id)) {
            throw Error(403, "origin is not allowed");
        }
        const std::string method = browser_http::header(request, "access-control-request-method");
        if (method != route_method(route)) {
            throw Error(403, "preflight method is not allowed");
        }
        const std::set<std::string> expected =
            method == "POST" ? std::set<std::string>{"content-type", "x-cfx-extension"}
                             : std::set<std::string>{"x-cfx-extension"};
        if (requested_headers(request) != expected) {
            throw Error(403, "preflight request headers are not allowed");
        }
        const std::string private_network =
            lower(browser_http::header(request, "access-control-request-private-network"));
        if (!private_network.empty() && private_network != "true") {
            throw Error(403, "invalid private-network preflight");
        }
        return origin;
    }

    RequestPolicy request_policy(const RequestHead& request, std::string& response_origin) const {
        require_host(request, server_.port());
        const std::optional<Route> route = find_route(request.target);
        if (!route || !active_route(mode_, *route)) {
            throw Error(404, "unknown browser connector endpoint");
        }

        if (request.method == "OPTIONS") {
            if (request.content_length.value_or(0) != 0) {
                throw Error(400, "preflight request must not have a body");
            }
            const std::string origin = browser_http::header(request, "origin");
            if (allowed_origin(origin, options_.extension_id)) {
                response_origin = origin;
            }
            return {*route, true, require_preflight(request, *route), 0};
        }
        if (request.method != route_method(*route)) {
            throw Error(405, "method is not allowed for browser connector endpoint");
        }

        const std::string origin = browser_http::header(request, "origin");
        if (allowed_origin(origin, options_.extension_id)) {
            response_origin = origin;
        }
        const std::string authenticated_origin = require_actual_client(request);
        if (request.method == "GET") {
            if (request.content_length.value_or(0) != 0) {
                throw Error(400, "GET request must not have a body");
            }
            return {*route, false, authenticated_origin, 0};
        }
        if (!request.content_length) {
            throw Error(411, "content length required");
        }
        if (!json_content_type(request)) {
            throw Error(415, "content type must be application/json");
        }
        const std::size_t maximum = *route == Route::fetch ? options_.max_fetch_bytes
                                                            : options_.max_result_bytes;
        if (*request.content_length > maximum) {
            throw Error(413, "request body too large");
        }
        return {*route, false, authenticated_origin, maximum};
    }

    Response handle(const Request& incoming, const RequestPolicy& policy,
                    BridgeCompletion& completion) {
        if (policy.preflight) {
            return response(204, {}, policy.origin, route_method(policy.route));
        }
        switch (policy.route) {
        case Route::ready:
            connector_ready_ = true;
            return response(200, "{\"ok\":true}", policy.origin);
        case Route::submission:
            if (submission_served_) {
                throw Error(409, "submission source was already requested");
            }
            submission_served_ = true;
            return response(200, submission_response(request_), policy.origin);
        case Route::result: {
            ParsedSubmitResult parsed = parse_submit_result(incoming.body);
            if (!submission_served_ && (parsed.error.empty() || parsed.unknown)) {
                throw Error(409, "submission source was not requested");
            }
            completion.identity = std::move(parsed.identity);
            completion.error = std::move(parsed.error);
            completion.submission_unknown = parsed.unknown;
            completion.complete = true;
            return response(200, "{\"ok\":true}", policy.origin);
        }
        case Route::fetch:
            completion.fetched_package = incoming.body;
            completion.complete = true;
            return response(200, "{\"ok\":true}", policy.origin);
        case Route::fetch_error:
            completion.error = result_error(incoming.body);
            completion.complete = true;
            return response(200, "{\"ok\":true}", policy.origin);
        }
        throw Error(404, "unknown browser connector endpoint");
    }

    BridgeMode mode_;
    std::string page_url_;
    BrowserSubmitRequest request_;
    BrowserBridgeOptions options_;
    std::string token_;
    std::string navigation_nonce_;
    browser_http::Server server_;
    bool connector_ready_ = false;
    bool submission_served_ = false;
};

std::string confirmed_submission_url(std::string_view contest, std::string_view submission_id) {
    return "https://codeforces.com/contest/" + std::string(contest) + "/submission/" +
           std::string(submission_id);
}

} // namespace

void open_browser_url(const std::string& url) {
    const BrowserCommand command = browser_command(url);
    if (command.detached) {
        launch_detached_process(command.arguments);
        return;
    }
    const ProcessResult result = run_process(command.arguments, ProcessOptions{
                                                                    .timeout =
                                                                        std::chrono::seconds(10),
                                                                });
    if (result.status != 0) {
        throw std::runtime_error("cannot open Chrome");
    }
}

std::string fetch_problem_in_browser(const std::string& page_url,
                                     const BrowserBridgeOptions& options) {
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + options.wait_timeout;
    BridgeCompletion completion;
    {
        Bridge bridge(BridgeMode::fetch, page_url, {}, options);
        bridge.open_browser();
        completion = bridge.wait(started, deadline);
    }
    if (!completion.error.empty()) {
        throw std::runtime_error("cannot fetch Codeforces problem: " + completion.error);
    }
    if (completion.fetched_package.empty()) {
        throw std::runtime_error("browser connector returned no problem data");
    }
    return completion.fetched_package;
}

BrowserSubmitReceipt submit_in_browser(const BrowserSubmitRequest& request,
                                       const BrowserBridgeOptions& options) {
    const auto started = std::chrono::steady_clock::now();
    const auto wall_started = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count();
    const auto deadline = started + options.wait_timeout;
    BridgeCompletion completion;
    {
        Bridge bridge(BridgeMode::submit, request.page_url, request, options);
        bridge.open_browser();
        completion = bridge.wait(started, deadline);
    }

    const std::string contest = contest_id(request.target);
    const std::string url = completion.identity.id.empty()
                                ? std::string()
                                : confirmed_submission_url(contest, completion.identity.id);
    if (!completion.error.empty()) {
        if (completion.submission_unknown) {
            std::string detail = completion.error;
            if (!completion.identity.id.empty()) {
                detail += "; submission " + completion.identity.id + "; " + url;
            }
            throw std::runtime_error("submission status is unknown: " + detail);
        }
        throw std::runtime_error("Codeforces submission failed: " + completion.error);
    }

    CodeforcesSubmission status;
    try {
        status = poll_submission_status(contest, request.index, completion.identity.handle,
                                        completion.identity.id, deadline,
                                        options.verdict_poll_interval);
    } catch (const std::exception& error) {
        throw std::runtime_error("submission status is unknown: " + std::string(error.what()) +
                                 "; submission " + completion.identity.id + "; " + url);
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    std::uint64_t judging_wait =
        static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed.count()));
    const auto wall_now = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
    if (completion.identity.submitted_at_millis > 0 &&
        completion.identity.submitted_at_millis <= static_cast<std::uint64_t>(wall_now) &&
        completion.identity.submitted_at_millis + 5000 >=
            static_cast<std::uint64_t>(wall_started)) {
        judging_wait = static_cast<std::uint64_t>(wall_now) -
                       completion.identity.submitted_at_millis;
    }
    return BrowserSubmitReceipt{
        url,
        completion.identity.id,
        status.verdict,
        status.verdict_text,
        status.passed_test_count,
        status.time_consumed_millis,
        status.memory_consumed_bytes,
        judging_wait,
    };
}

} // namespace cfx
