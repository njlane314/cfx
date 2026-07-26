#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cfx::browser_http {

using Headers = std::map<std::string, std::string, std::less<>>;

class Error final : public std::runtime_error {
  public:
    Error(int status, std::string message);

    [[nodiscard]] int status() const noexcept;

  private:
    int status_;
};

struct RequestHead {
    std::string method;
    std::string target;
    Headers headers;
    std::optional<std::size_t> content_length;
};

struct Request : RequestHead {
    std::string body;
};

struct Response {
    int status = 200;
    Headers headers;
    std::string body;
};

[[nodiscard]] std::string header(const RequestHead& request, std::string_view name);

class Connection {
  public:
    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    Request read(const std::chrono::steady_clock::time_point& deadline,
                 const std::function<std::size_t(const RequestHead&)>& body_limit);
    void send(const Response& response) const;

  private:
    friend class Server;
    Connection(int descriptor, bool loopback);

    int descriptor_ = -1;
    bool loopback_ = false;
};

class Server {
  public:
    Server();
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    [[nodiscard]] std::uint16_t port() const noexcept;
    std::optional<Connection>
    accept_until(const std::chrono::steady_clock::time_point& deadline,
                 std::chrono::milliseconds request_timeout) const;

  private:
    int descriptor_ = -1;
    std::uint16_t port_ = 0;
};

} // namespace cfx::browser_http
