#include "browser_http.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

namespace cfx::browser_http {
namespace {

constexpr std::size_t kMaxHeaderBytes = 16 * 1024;

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
        throw std::runtime_error(system_error("cannot protect HTTP descriptor"));
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
        throw std::runtime_error(system_error("cannot set HTTP timeout"));
    }
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    if (::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0) {
        throw std::runtime_error(system_error("cannot protect HTTP socket"));
    }
#endif
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string trim(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() && (value[first] == ' ' || value[first] == '\t')) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && (value[last - 1] == ' ' || value[last - 1] == '\t')) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

bool token_character(unsigned char character) {
    const bool alphanumeric = (character >= '0' && character <= '9') ||
                              (character >= 'A' && character <= 'Z') ||
                              (character >= 'a' && character <= 'z');
    return alphanumeric ||
           std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(character)) !=
               std::string_view::npos;
}

bool field_value_character(unsigned char character) {
    return character == '\t' || (character >= 0x20U && character != 0x7fU);
}

std::string receive_some(int descriptor, const std::chrono::steady_clock::time_point& deadline) {
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            throw Error(408, "request timed out");
        }
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const int timeout = static_cast<int>(
            std::max<std::int64_t>(1, std::min<std::int64_t>(remaining.count(), 60000)));
        pollfd ready{descriptor, POLLIN, 0};
        const int result = ::poll(&ready, 1, timeout);
        if (result == 0) {
            throw Error(408, "request timed out");
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw Error(400, "cannot wait for request");
        }
        break;
    }

    std::array<char, 4096> buffer{};
    const ssize_t count = ::recv(descriptor, buffer.data(), buffer.size(), 0);
    if (count > 0) {
        return std::string(buffer.data(), static_cast<std::size_t>(count));
    }
    if (count == 0) {
        throw Error(400, "connection closed before request completed");
    }
    if (errno == EINTR) {
        return {};
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        throw Error(408, "request timed out");
    }
    throw Error(400, "cannot read request");
}

std::optional<std::size_t> parse_content_length(const RequestHead& request) {
    const std::string value = header(request, "content-length");
    if (value.empty()) {
        return std::nullopt;
    }
    std::size_t result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc() || end != value.data() + value.size()) {
        throw Error(400, "invalid content length");
    }
    return result;
}

std::string status_text(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 204:
        return "No Content";
    case 400:
        return "Bad Request";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 408:
        return "Request Timeout";
    case 409:
        return "Conflict";
    case 411:
        return "Length Required";
    case 413:
        return "Payload Too Large";
    case 415:
        return "Unsupported Media Type";
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

} // namespace

Error::Error(int status, std::string message)
    : std::runtime_error(std::move(message)), status_(status) {}

int Error::status() const noexcept {
    return status_;
}

std::string header(const RequestHead& request, std::string_view name) {
    const auto found = request.headers.find(name);
    return found == request.headers.end() ? std::string() : found->second;
}

Connection::Connection(int descriptor, bool loopback)
    : descriptor_(descriptor), loopback_(loopback) {}

Connection::Connection(Connection&& other) noexcept
    : descriptor_(std::exchange(other.descriptor_, -1)), loopback_(other.loopback_) {}

Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        close_descriptor(descriptor_);
        descriptor_ = std::exchange(other.descriptor_, -1);
        loopback_ = other.loopback_;
    }
    return *this;
}

Connection::~Connection() {
    close_descriptor(descriptor_);
}

Request Connection::read(const std::chrono::steady_clock::time_point& deadline,
                         const std::function<std::size_t(const RequestHead&)>& body_limit) {
    if (!loopback_) {
        throw Error(403, "HTTP server only accepts loopback clients");
    }

    std::string input;
    std::size_t separator = std::string::npos;
    while ((separator = input.find("\r\n\r\n")) == std::string::npos) {
        if (input.size() >= kMaxHeaderBytes) {
            throw Error(431, "request headers too large");
        }
        input += receive_some(descriptor_, deadline);
    }
    if (separator > kMaxHeaderBytes) {
        throw Error(431, "request headers too large");
    }

    const std::size_t request_line_end = input.find("\r\n");
    if (request_line_end == std::string::npos) {
        throw Error(400, "invalid request line");
    }
    const std::string_view request_line(input.data(), request_line_end);
    const std::size_t first_space = request_line.find(' ');
    const std::size_t second_space = first_space == std::string_view::npos
                                         ? std::string_view::npos
                                         : request_line.find(' ', first_space + 1);
    if (first_space == 0 || first_space == std::string_view::npos ||
        second_space == first_space + 1 || second_space == std::string_view::npos ||
        request_line.substr(second_space + 1) != "HTTP/1.1") {
        throw Error(400, "invalid request line");
    }

    Request request;
    request.method = std::string(request_line.substr(0, first_space));
    request.target =
        std::string(request_line.substr(first_space + 1, second_space - first_space - 1));
    if (!std::all_of(request.method.begin(), request.method.end(), token_character) ||
        request.target.empty() || request.target.front() != '/' ||
        !std::all_of(request.target.begin(), request.target.end(), [](unsigned char character) {
            return character >= 0x21U && character != 0x7fU;
        })) {
        throw Error(400, "invalid request line");
    }

    std::size_t line_start = request_line_end + 2;
    while (line_start < separator) {
        const std::size_t line_end = input.find("\r\n", line_start);
        if (line_end == std::string::npos || line_end > separator) {
            throw Error(400, "invalid request headers");
        }
        const std::string_view line(input.data() + line_start, line_end - line_start);
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) {
            throw Error(400, "invalid request header");
        }
        const std::string_view raw_name = line.substr(0, colon);
        if (raw_name.empty() || !std::all_of(raw_name.begin(), raw_name.end(), token_character)) {
            throw Error(400, "invalid request header name");
        }
        const std::string_view raw_value = line.substr(colon + 1);
        if (!std::all_of(raw_value.begin(), raw_value.end(), field_value_character)) {
            throw Error(400, "invalid request header value");
        }
        const std::string name = lower(std::string(raw_name));
        const std::string value = trim(raw_value);
        if (request.headers.contains(name)) {
            throw Error(400, "invalid duplicate request header");
        }
        request.headers.emplace(name, value);
        line_start = line_end + 2;
    }

    if (!header(request, "transfer-encoding").empty()) {
        throw Error(400, "transfer encoding is unsupported");
    }
    request.content_length = parse_content_length(request);
    const std::size_t maximum_body = body_limit(request);
    const std::size_t length = request.content_length.value_or(0);
    if (length > maximum_body) {
        throw Error(413, "request body too large");
    }

    const std::size_t body_start = separator + 4;
    while (input.size() - body_start < length) {
        input += receive_some(descriptor_, deadline);
        if (input.size() - body_start > maximum_body) {
            throw Error(413, "request body too large");
        }
    }
    if (input.size() - body_start != length) {
        throw Error(400, "unexpected bytes after request body");
    }
    request.body.assign(input.data() + body_start, length);
    return request;
}

void Connection::send(const Response& response) const {
    std::string output = "HTTP/1.1 " + std::to_string(response.status) + " " +
                         status_text(response.status) + "\r\nConnection: close\r\n";
    for (const auto& [name, value] : response.headers) {
        output += name + ": " + value + "\r\n";
    }
    output += "Content-Length: " + std::to_string(response.body.size()) + "\r\n\r\n" +
              response.body;
    send_all(descriptor_, output);
}

Server::Server() {
    descriptor_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (descriptor_ < 0) {
        throw std::runtime_error(system_error("cannot create HTTP server"));
    }
    try {
        set_close_on_exec(descriptor_);
        const int enabled = 1;
        if (::setsockopt(descriptor_, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) != 0) {
            throw std::runtime_error(system_error("cannot configure HTTP server"));
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(0);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(descriptor_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            throw std::runtime_error(system_error("cannot bind HTTP server"));
        }
        if (::listen(descriptor_, 8) != 0) {
            throw std::runtime_error(system_error("cannot listen for HTTP clients"));
        }
        socklen_t size = sizeof(address);
        if (::getsockname(descriptor_, reinterpret_cast<sockaddr*>(&address), &size) != 0) {
            throw std::runtime_error(system_error("cannot inspect HTTP server"));
        }
        port_ = ntohs(address.sin_port);
    } catch (...) {
        close_descriptor(descriptor_);
        throw;
    }
}

Server::~Server() {
    close_descriptor(descriptor_);
}

std::uint16_t Server::port() const noexcept {
    return port_;
}

std::optional<Connection>
Server::accept_until(const std::chrono::steady_clock::time_point& deadline,
                     std::chrono::milliseconds request_timeout) const {
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return std::nullopt;
        }
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const int timeout = static_cast<int>(
            std::max<std::int64_t>(1, std::min<std::int64_t>(remaining.count(), 60000)));
        pollfd ready{descriptor_, POLLIN, 0};
        const int result = ::poll(&ready, 1, timeout);
        if (result == 0) {
            return std::nullopt;
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error(system_error("cannot wait for HTTP client"));
        }

        sockaddr_in peer{};
        socklen_t peer_size = sizeof(peer);
        const int connection =
            ::accept(descriptor_, reinterpret_cast<sockaddr*>(&peer), &peer_size);
        if (connection < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error(system_error("cannot accept HTTP client"));
        }
        try {
            set_close_on_exec(connection);
            set_socket_timeout(connection, request_timeout);
        } catch (...) {
            (void)::close(connection);
            throw;
        }
        const bool loopback =
            peer.sin_family == AF_INET && peer.sin_addr.s_addr == htonl(INADDR_LOOPBACK);
        return Connection(connection, loopback);
    }
}

} // namespace cfx::browser_http
