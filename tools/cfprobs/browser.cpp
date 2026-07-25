#include "browser.hpp"

#include "json.hpp"
#include "process.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <netinet/in.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace cfprobs {
namespace {

constexpr std::size_t kMaxHeaderBytes = 16 * 1024;

class HttpError final : public std::runtime_error {
  public:
    HttpError(int status, std::string message)
        : std::runtime_error(std::move(message)), status_(status) {}

    [[nodiscard]] int status() const noexcept {
        return status_;
    }

  private:
    int status_;
};

struct HttpRequest {
    std::string method;
    std::string target;
    std::map<std::string, std::string, std::less<>> headers;
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string content_type = "application/json; charset=utf-8";
    std::string body;
    std::string origin;
    bool preflight = false;
};

std::string system_error(std::string_view operation) {
    return std::string(operation) + ": " + std::strerror(errno);
}

void close_descriptor(int& descriptor) {
    if (descriptor >= 0) {
        (void)::close(descriptor);
        descriptor = -1;
    }
}

void set_close_on_exec(int descriptor) {
    const int flags = ::fcntl(descriptor, F_GETFD);
    if (flags < 0 || ::fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) < 0) {
        throw std::runtime_error(system_error("cannot protect browser bridge descriptor"));
    }
}

void set_socket_timeout(int descriptor, std::chrono::milliseconds timeout) {
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    const auto remainder = std::chrono::duration_cast<std::chrono::microseconds>(timeout - seconds);
    timeval value{
        static_cast<time_t>(seconds.count()),
        static_cast<suseconds_t>(remainder.count()),
    };
    if (::setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value)) != 0 ||
        ::setsockopt(descriptor, SOL_SOCKET, SO_SNDTIMEO, &value, sizeof(value)) != 0) {
        throw std::runtime_error(system_error("cannot set browser bridge timeout"));
    }
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    if (::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0) {
        throw std::runtime_error(system_error("cannot protect browser bridge socket"));
    }
#endif
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
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '-' || character == '_';
    });
}

bool allowed_origin(std::string_view origin) {
    if (origin == "https://codeforces.com") {
        return true;
    }
    for (const std::string_view prefix :
         {"chrome-extension://", "moz-extension://", "safari-web-extension://"}) {
        if (origin.starts_with(prefix) && extension_identifier(origin.substr(prefix.size()))) {
            return true;
        }
    }
    return false;
}

std::string header(const HttpRequest& request, std::string_view name) {
    const auto found = request.headers.find(name);
    return found == request.headers.end() ? std::string() : found->second;
}

std::size_t content_length(const HttpRequest& request, std::size_t maximum) {
    const std::string value = header(request, "content-length");
    if (value.empty()) {
        throw HttpError(411, "content length required");
    }
    std::size_t result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc() || end != value.data() + value.size()) {
        throw HttpError(400, "invalid content length");
    }
    if (result > maximum) {
        throw HttpError(413, "request body too large");
    }
    return result;
}

std::string receive_some(int descriptor, const std::chrono::steady_clock::time_point& deadline) {
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            throw HttpError(408, "request timed out");
        }
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const int timeout = static_cast<int>(
            std::max<std::int64_t>(1, std::min<std::int64_t>(remaining.count(), 60000)));
        pollfd ready{descriptor, POLLIN, 0};
        const int result = ::poll(&ready, 1, timeout);
        if (result == 0) {
            throw HttpError(408, "request timed out");
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw HttpError(400, "cannot wait for request");
        }
        break;
    }

    std::array<char, 4096> buffer{};
    const ssize_t count = ::recv(descriptor, buffer.data(), buffer.size(), 0);
    if (count > 0) {
        return std::string(buffer.data(), static_cast<std::size_t>(count));
    }
    if (count == 0) {
        throw HttpError(400, "connection closed before request completed");
    }
    if (errno == EINTR) {
        return {};
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        throw HttpError(408, "request timed out");
    }
    throw HttpError(400, "cannot read request");
}

HttpRequest read_request(int descriptor, std::size_t maximum_body,
                         const std::chrono::steady_clock::time_point& deadline) {
    std::string input;
    std::size_t separator = std::string::npos;
    while ((separator = input.find("\r\n\r\n")) == std::string::npos) {
        if (input.size() >= kMaxHeaderBytes) {
            throw HttpError(431, "request headers too large");
        }
        input += receive_some(descriptor, deadline);
        if (input.size() > kMaxHeaderBytes + maximum_body) {
            throw HttpError(413, "request too large");
        }
    }
    if (separator > kMaxHeaderBytes) {
        throw HttpError(431, "request headers too large");
    }

    const std::size_t request_line_end = input.find("\r\n");
    if (request_line_end == std::string::npos) {
        throw HttpError(400, "invalid request line");
    }
    const std::string_view request_line(input.data(), request_line_end);
    const std::size_t first_space = request_line.find(' ');
    const std::size_t second_space = first_space == std::string_view::npos
                                         ? std::string_view::npos
                                         : request_line.find(' ', first_space + 1);
    if (first_space == std::string_view::npos || second_space == std::string_view::npos ||
        request_line.substr(second_space + 1) != "HTTP/1.1") {
        throw HttpError(400, "invalid request line");
    }

    HttpRequest request;
    request.method = std::string(request_line.substr(0, first_space));
    request.target =
        std::string(request_line.substr(first_space + 1, second_space - first_space - 1));

    std::size_t line_start = request_line_end + 2;
    while (line_start < separator) {
        const std::size_t line_end = input.find("\r\n", line_start);
        if (line_end == std::string::npos || line_end > separator) {
            throw HttpError(400, "invalid request headers");
        }
        const std::string_view line(input.data() + line_start, line_end - line_start);
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) {
            throw HttpError(400, "invalid request header");
        }
        const std::string name = lower(trim(line.substr(0, colon)));
        const std::string value = trim(line.substr(colon + 1));
        if (name.empty() || request.headers.contains(name)) {
            throw HttpError(400, "invalid duplicate request header");
        }
        request.headers.emplace(name, value);
        line_start = line_end + 2;
    }

    if (!header(request, "transfer-encoding").empty()) {
        throw HttpError(400, "transfer encoding is unsupported");
    }
    const std::size_t length = request.method == "POST" ? content_length(request, maximum_body) : 0;
    const std::size_t body_start = separator + 4;
    while (input.size() - body_start < length) {
        input += receive_some(descriptor, deadline);
        if (input.size() - body_start > maximum_body) {
            throw HttpError(413, "request body too large");
        }
    }
    request.body.assign(input.data() + body_start, length);
    return request;
}

std::string status_text(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 204:
        return "No Content";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 408:
        return "Request Timeout";
    case 409:
        return "Conflict";
    case 411:
        return "Length Required";
    case 413:
        return "Payload Too Large";
    case 431:
        return "Request Header Fields Too Large";
    default:
        return "Error";
    }
}

void send_all(int descriptor, std::string_view value) {
    std::size_t offset = 0;
    while (offset < value.size()) {
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const ssize_t count =
            ::send(descriptor, value.data() + offset, value.size() - offset, flags);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return;
        }
    }
}

void send_response(int descriptor, const HttpResponse& response) {
    std::string output = "HTTP/1.1 " + std::to_string(response.status) + " " +
                         status_text(response.status) +
                         "\r\n"
                         "Connection: close\r\n"
                         "Cache-Control: no-store\r\n"
                         "X-Content-Type-Options: nosniff\r\n"
                         "Referrer-Policy: no-referrer\r\n";
    if (!response.origin.empty()) {
        output += "Access-Control-Allow-Origin: " + response.origin +
                  "\r\n"
                  "Access-Control-Allow-Private-Network: true\r\n"
                  "Vary: Origin\r\n";
    }
    if (response.preflight) {
        output += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                  "Access-Control-Allow-Headers: Content-Type\r\n"
                  "Access-Control-Max-Age: 60\r\n";
    }
    output += "Content-Type: " + response.content_type +
              "\r\n"
              "Content-Length: " +
              std::to_string(response.body.size()) + "\r\n\r\n" + response.body;
    send_all(descriptor, output);
}

std::string required_origin(const HttpRequest& request) {
    const std::string origin = header(request, "origin");
    if (origin.empty() || !allowed_origin(origin)) {
        throw HttpError(403, "origin is not allowed");
    }
    return origin;
}

void require_host(const HttpRequest& request, std::uint16_t port) {
    const std::string expected = "127.0.0.1:" + std::to_string(port);
    if (header(request, "host") != expected) {
        throw HttpError(403, "invalid host");
    }
}

std::string optional_string(const Json& document, std::string_view name) {
    const Json* value = document.find(name);
    if (value == nullptr) {
        return {};
    }
    if (!value->is_string()) {
        throw HttpError(400, std::string(name) + " must be a string");
    }
    return value->string();
}

bool required_bool(const Json& document, std::string_view name) {
    const Json* value = document.find(name);
    if (value == nullptr || !value->is_bool()) {
        throw HttpError(400, std::string(name) + " must be a boolean");
    }
    return value->boolean();
}

struct ParsedSubmitResult {
    BrowserSubmitReceipt receipt;
    std::string error;
};

ParsedSubmitResult parse_submit_result(std::string_view body) {
    Json document;
    try {
        document = parse_json(body);
    } catch (const JsonError&) {
        throw HttpError(400, "result is not valid JSON");
    }
    if (!document.is_object()) {
        throw HttpError(400, "result must be a JSON object");
    }
    const bool ok = required_bool(document, "ok");
    ParsedSubmitResult result{
        BrowserSubmitReceipt{
            optional_string(document, "url"),
            optional_string(document, "verdict"),
        },
        optional_string(document, "error"),
    };
    if (result.error.empty()) {
        result.error = optional_string(document, "message");
    }
    if (ok && result.receipt.submission_url.empty()) {
        throw HttpError(400, "successful result has no submission URL");
    }
    if (!ok && result.error.empty()) {
        result.error = "Codeforces rejected the submission";
    }
    if (ok) {
        result.error.clear();
    }
    return result;
}

std::string submission_response(const BrowserSubmitRequest& request) {
    return "{\"target\":" + json_quote(request.target) + ",\"index\":" + json_quote(request.index) +
           ",\"language\":" + json_quote(request.language) +
           ",\"source\":" + json_quote(request.source) + "}";
}

std::vector<std::string> browser_command(std::string_view url) {
    const char* configured = std::getenv("CFPROBS_BROWSER");
    if (configured != nullptr && *configured != '\0') {
        std::vector<std::string> command = split_command_words(configured);
        if (command.empty()) {
            throw std::runtime_error("CFPROBS_BROWSER is empty");
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
        return command;
    }
#ifdef __APPLE__
    return {"open", std::string(url)};
#else
    return {"xdg-open", std::string(url)};
#endif
}

enum class BridgeMode { fetch, submit };

struct BridgeCompletion {
    std::string fetched_package;
    BrowserSubmitReceipt submission;
    std::string error;
    bool complete = false;
};

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

class Bridge {
  public:
    Bridge(BridgeMode mode, std::string page_url, BrowserSubmitRequest request,
           BrowserBridgeOptions options)
        : mode_(mode), page_url_(std::move(page_url)), request_(std::move(request)),
          options_(options), token_(random_hex(32)), navigation_nonce_(random_hex(8)) {
        validate();
        listen();
    }

    ~Bridge() {
        close_descriptor(listener_);
        std::fill(token_.begin(), token_.end(), '\0');
        std::fill(navigation_nonce_.begin(), navigation_nonce_.end(), '\0');
        std::fill(request_.source.begin(), request_.source.end(), '\0');
    }

    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;

    void open_browser() const {
        const ProcessResult result =
            run_process(browser_command(launch_url()), ProcessOptions{
                                                           std::nullopt,
                                                           std::nullopt,
                                                           std::nullopt,
                                                           std::chrono::seconds(10),
                                                           std::nullopt,
                                                       });
        if (result.status != 0) {
            throw std::runtime_error("cannot open the browser");
        }
    }

    BridgeCompletion wait() {
        BridgeCompletion completion;
        std::size_t connections = 0;
        const auto deadline = std::chrono::steady_clock::now() + options_.wait_timeout;

        while (!completion.complete) {
            if (connections >= options_.max_connections) {
                throw std::runtime_error("browser connector sent too many requests");
            }
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                throw std::runtime_error(
                    "browser connector did not respond; load the repository's browser/ "
                    "extension and retry");
            }
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            const int timeout = static_cast<int>(
                std::min<std::int64_t>(remaining.count(), static_cast<std::int64_t>(60000)));
            pollfd descriptor{listener_, POLLIN, 0};
            const int ready = ::poll(&descriptor, 1, timeout);
            if (ready == 0) {
                continue;
            }
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error(system_error("cannot wait for browser connector"));
            }

            sockaddr_in peer{};
            socklen_t peer_size = sizeof(peer);
            int connection = ::accept(listener_, reinterpret_cast<sockaddr*>(&peer), &peer_size);
            if (connection < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error(system_error("cannot accept browser connector request"));
            }
            ++connections;
            std::string response_origin;
            try {
                set_close_on_exec(connection);
                set_socket_timeout(connection, options_.request_timeout);
                if (peer.sin_family != AF_INET || peer.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
                    throw HttpError(403, "browser connector only accepts loopback clients");
                }
                const std::size_t maximum_body =
                    std::max(options_.max_fetch_bytes, options_.max_result_bytes);
                const auto request_deadline =
                    std::min(deadline, std::chrono::steady_clock::now() + options_.request_timeout);
                const HttpRequest incoming =
                    read_request(connection, maximum_body, request_deadline);
                const std::string origin = header(incoming, "origin");
                if (allowed_origin(origin)) {
                    response_origin = origin;
                }
                send_response(connection, handle(incoming, completion));
            } catch (const HttpError& error) {
                send_response(connection, HttpResponse{
                                              error.status(),
                                              "application/json; charset=utf-8",
                                              "{\"error\":" + json_quote(error.what()) + "}",
                                              response_origin,
                                              false,
                                          });
            } catch (const std::exception&) {
                send_response(connection, HttpResponse{
                                              400,
                                              "application/json; charset=utf-8",
                                              "{\"error\":\"invalid request\"}",
                                              response_origin,
                                              false,
                                          });
            }
            close_descriptor(connection);
        }
        close_descriptor(listener_);
        return completion;
    }

  private:
    void validate() const {
        if (page_url_.empty()) {
            throw std::invalid_argument("browser page URL is empty");
        }
        if (mode_ == BridgeMode::submit && (request_.target.empty() || request_.index.empty() ||
                                            request_.language.empty() || request_.source.empty())) {
            throw std::invalid_argument("browser submission is incomplete");
        }
        if (request_.source.size() > options_.max_source_bytes) {
            throw std::invalid_argument("browser submission source is too large");
        }
        if (options_.request_timeout <= std::chrono::milliseconds::zero() ||
            options_.wait_timeout <= std::chrono::milliseconds::zero() ||
            options_.max_fetch_bytes == 0 || options_.max_result_bytes == 0 ||
            options_.max_connections == 0) {
            throw std::invalid_argument("browser connector limits must be positive");
        }
    }

    void listen() {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener_ < 0) {
            throw std::runtime_error(system_error("cannot create browser connector"));
        }
        try {
            set_close_on_exec(listener_);
            const int enabled = 1;
            if (::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) != 0) {
                throw std::runtime_error(system_error("cannot configure browser connector"));
            }
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(0);
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (::bind(listener_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) !=
                0) {
                throw std::runtime_error(system_error("cannot bind browser connector"));
            }
            if (::listen(listener_, 8) != 0) {
                throw std::runtime_error(system_error("cannot listen for browser connector"));
            }
            socklen_t size = sizeof(address);
            if (::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size) != 0) {
                throw std::runtime_error(system_error("cannot inspect browser connector"));
            }
            port_ = ntohs(address.sin_port);
        } catch (...) {
            close_descriptor(listener_);
            throw;
        }
    }

    [[nodiscard]] std::string launch_url() const {
        std::string url = page_url_.substr(0, page_url_.find('#'));
        url += url.find('?') == std::string::npos ? '?' : '&';
        url += "cfprobs_reload=" + navigation_nonce_;
        url += "#cfprobs=";
        url += mode_ == BridgeMode::fetch ? "fetch" : "submit";
        url += "&port=" + std::to_string(port_) + "&token=" + token_;
        return url;
    }

    [[nodiscard]] bool route(std::string_view target, std::string_view name) const {
        return constant_time_equal(target, "/" + std::string(name) + "/" + token_);
    }

    HttpResponse handle(const HttpRequest& incoming, BridgeCompletion& completion) {
        require_host(incoming, port_);
        const bool known_route =
            route(incoming.target, "submission") || route(incoming.target, "fetch") ||
            route(incoming.target, "fetch-error") || route(incoming.target, "result");
        if (incoming.method == "OPTIONS" && known_route) {
            return HttpResponse{
                204, "text/plain; charset=utf-8", {}, required_origin(incoming), true,
            };
        }

        const std::string origin = required_origin(incoming);
        if (mode_ == BridgeMode::submit && incoming.method == "GET" &&
            route(incoming.target, "submission")) {
            if (submission_served_) {
                throw HttpError(409, "submission source was already requested");
            }
            submission_served_ = true;
            return HttpResponse{
                200,   "application/json; charset=utf-8", submission_response(request_), origin,
                false,
            };
        }
        if (mode_ == BridgeMode::submit && incoming.method == "POST" &&
            route(incoming.target, "result")) {
            if (incoming.body.size() > options_.max_result_bytes) {
                throw HttpError(413, "submission result is too large");
            }
            if (!submission_served_) {
                throw HttpError(409, "submission source was not requested");
            }
            ParsedSubmitResult parsed = parse_submit_result(incoming.body);
            completion.submission = std::move(parsed.receipt);
            completion.error = std::move(parsed.error);
            completion.complete = true;
            return HttpResponse{
                200, "application/json; charset=utf-8", "{\"ok\":true}", origin, false,
            };
        }
        if (mode_ == BridgeMode::fetch && incoming.method == "POST" &&
            route(incoming.target, "fetch")) {
            if (incoming.body.size() > options_.max_fetch_bytes) {
                throw HttpError(413, "fetched problem is too large");
            }
            completion.fetched_package = incoming.body;
            completion.complete = true;
            return HttpResponse{
                200, "application/json; charset=utf-8", "{\"ok\":true}", origin, false,
            };
        }
        if (mode_ == BridgeMode::fetch && incoming.method == "POST" &&
            route(incoming.target, "fetch-error")) {
            if (incoming.body.size() > options_.max_result_bytes) {
                throw HttpError(413, "fetch error is too large");
            }
            completion.error = result_error(incoming.body);
            completion.complete = true;
            return HttpResponse{
                200, "application/json; charset=utf-8", "{\"ok\":true}", origin, false,
            };
        }
        throw HttpError(404, "unknown browser connector endpoint");
    }

    BridgeMode mode_;
    std::string page_url_;
    BrowserSubmitRequest request_;
    BrowserBridgeOptions options_;
    int listener_ = -1;
    std::uint16_t port_ = 0;
    std::string token_;
    std::string navigation_nonce_;
    bool submission_served_ = false;
};

} // namespace

std::string fetch_problem_in_browser(const std::string& page_url,
                                     const BrowserBridgeOptions& options) {
    Bridge bridge(BridgeMode::fetch, page_url, {}, options);
    bridge.open_browser();
    BridgeCompletion completion = bridge.wait();
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
    Bridge bridge(BridgeMode::submit, request.page_url, request, options);
    bridge.open_browser();
    BridgeCompletion completion = bridge.wait();
    if (!completion.error.empty()) {
        throw std::runtime_error("Codeforces submission failed: " + completion.error);
    }
    return completion.submission;
}

} // namespace cfprobs
