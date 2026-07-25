#include "companion.hpp"

#include "json.hpp"
#include "workspace.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <netdb.h>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>

namespace cfx {
namespace {

namespace fs = std::filesystem;

std::string read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(),
    };
}

void write_file(const fs::path& path, const std::string& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output || !(output << value)) {
        throw std::runtime_error("cannot write " + path.string());
    }
}

void write_file_atomically(const fs::path& path, const std::string& value) {
    if (fs::is_regular_file(path) && read_file(path) == value) {
        return;
    }
    const fs::path temporary = path.string() + ".tmp." + std::to_string(::getpid());
    write_file(temporary, value);
    std::error_code error;
    fs::rename(temporary, path, error);
    if (error) {
        fs::remove(temporary, error);
        throw std::runtime_error("cannot replace " + path.string());
    }
}

std::string with_final_newline(std::string value) {
    if (value.empty() || value.back() != '\n') {
        value.push_back('\n');
    }
    return value;
}

int integer_field(const Json& object, std::string_view name) {
    const Json* value = object.find(name);
    if (value == nullptr) {
        return 0;
    }
    if (!value->is_number() || value->number() < 0.0 ||
        value->number() > static_cast<double>(std::numeric_limits<int>::max()) ||
        std::trunc(value->number()) != value->number()) {
        throw std::runtime_error("problem package field '" + std::string(name) +
                                 "' must be a non-negative integer");
    }
    return static_cast<int>(value->number());
}

std::string string_field(const Json& object, std::string_view name) {
    const Json* value = object.find(name);
    return value != nullptr && value->is_string() ? value->string() : std::string{};
}

std::string sample_name(std::size_t number, const char* extension) {
    std::ostringstream output;
    output << std::setw(2) << std::setfill('0') << number << extension;
    return output.str();
}

void recover_sample_transaction(const Problem& problem) {
    const fs::path directory = problem.directory();
    if (!fs::is_directory(directory)) {
        return;
    }
    const fs::path samples = problem.samples_path();
    const fs::path backup = directory / ".samples.backup";
    std::error_code error;
    if (fs::is_directory(backup)) {
        if (!fs::exists(samples)) {
            fs::rename(backup, samples, error);
            if (error) {
                throw std::runtime_error("cannot recover fetched samples: " + error.message());
            }
        } else {
            fs::remove_all(backup, error);
        }
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
        const std::string name = entry.path().filename().string();
        if (entry.is_directory() && name.starts_with(".samples.stage.")) {
            fs::remove_all(entry.path(), error);
        }
    }
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return value;
}

std::size_t content_length(std::string_view headers) {
    std::size_t start = 0;
    while (start < headers.size()) {
        const std::size_t end = headers.find("\r\n", start);
        const std::string_view line =
            headers.substr(start, (end == std::string_view::npos ? headers.size() : end) - start);
        const std::size_t colon = line.find(':');
        if (colon != std::string_view::npos &&
            lowercase(std::string(line.substr(0, colon))) == "content-length") {
            std::string value(line.substr(colon + 1));
            std::size_t parsed = 0;
            const unsigned long long length = std::stoull(value, &parsed);
            while (parsed < value.size() &&
                   std::isspace(static_cast<unsigned char>(value[parsed])) != 0) {
                ++parsed;
            }
            if (parsed != value.size() || length > 16ULL * 1024ULL * 1024ULL) {
                throw std::runtime_error("invalid HTTP Content-Length");
            }
            return static_cast<std::size_t>(length);
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 2;
    }
    throw std::runtime_error("HTTP request has no Content-Length");
}

std::string receive_request(int client) {
    std::string request;
    char buffer[8192];
    std::optional<std::size_t> wanted;
    std::size_t header_size = 0;

    while (true) {
        const ssize_t count = ::recv(client, buffer, sizeof(buffer), 0);
        if (count > 0) {
            request.append(buffer, static_cast<std::size_t>(count));
        } else if (count == 0) {
            break;
        } else if (errno != EINTR) {
            throw std::runtime_error("cannot read HTTP request: " +
                                     std::string(std::strerror(errno)));
        }

        if (!wanted) {
            const std::size_t marker = request.find("\r\n\r\n");
            if (marker != std::string::npos) {
                header_size = marker + 4;
                wanted = content_length(std::string_view(request).substr(0, marker + 2));
            }
        }
        if (wanted && request.size() >= header_size + *wanted) {
            return request.substr(0, header_size + *wanted);
        }
        if (request.size() > 16U * 1024U * 1024U + 64U * 1024U) {
            throw std::runtime_error("HTTP request is too large");
        }
    }
    throw std::runtime_error("incomplete HTTP request");
}

void send_response(int client, int status, const std::string& message) {
    const std::string reason = status == 200 ? "OK" : "Bad Request";
    const std::string response = "HTTP/1.1 " + std::to_string(status) + " " + reason +
                                 "\r\n"
                                 "Content-Type: text/plain; charset=utf-8\r\n"
                                 "Content-Length: " +
                                 std::to_string(message.size()) +
                                 "\r\n"
                                 "Connection: close\r\n\r\n" +
                                 message;
    std::size_t sent = 0;
    while (sent < response.size()) {
        const ssize_t count = ::send(client, response.data() + sent, response.size() - sent, 0);
        if (count > 0) {
            sent += static_cast<std::size_t>(count);
        } else if (count == 0 || (count < 0 && errno != EINTR)) {
            return;
        }
    }
}

int listening_socket(const std::string& host, int port) {
    if (port < 1 || port > 65535) {
        throw std::runtime_error("port must be between 1 and 65535");
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    addrinfo* addresses = nullptr;
    const std::string service = std::to_string(port);
    const int resolved =
        ::getaddrinfo(host.empty() ? nullptr : host.c_str(), service.c_str(), &hints, &addresses);
    if (resolved != 0) {
        throw std::runtime_error("cannot resolve listener address: " +
                                 std::string(::gai_strerror(resolved)));
    }

    int server = -1;
    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        server = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (server < 0) {
            continue;
        }
        int enabled = 1;
        (void)::setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
        if (::bind(server, address->ai_addr, address->ai_addrlen) == 0 &&
            ::listen(server, 8) == 0) {
            break;
        }
        ::close(server);
        server = -1;
    }
    ::freeaddrinfo(addresses);
    if (server < 0) {
        throw std::runtime_error("cannot listen on " + host + ":" + std::to_string(port));
    }
    return server;
}

} // namespace

CompanionPackage parse_companion_package(std::string_view payload, const fs::path& root) {
    const Json document = parse_json(payload);
    const std::string url = string_field(document, "url");
    if (url.empty()) {
        throw std::runtime_error("problem package has no URL");
    }
    CompanionPackage package{
        Problem::parse(url, root),
        url,
        string_field(document, "name"),
        integer_field(document, "timeLimit"),
        integer_field(document, "memoryLimit"),
        {},
    };
    const Json* tests = document.find("tests");
    if (tests == nullptr || !tests->is_array()) {
        throw std::runtime_error("problem package has no tests");
    }
    for (const Json& test : tests->array()) {
        package.samples.push_back(Sample{
            string_field(test, "input"),
            string_field(test, "output"),
        });
    }
    if (package.samples.empty()) {
        throw std::runtime_error("problem package has no tests");
    }
    return package;
}

ImportResult import_companion_package(const CompanionPackage& package, const fs::path& root,
                                      bool force) {
    recover_sample_transaction(package.problem);
    Workspace(root).create(package.problem);
    const fs::path samples = package.problem.samples_path();

    struct Expected {
        std::string input_name;
        std::string output_name;
        std::string input;
        std::string output;
    };
    std::vector<Expected> expected;
    for (std::size_t index = 0; index < package.samples.size(); ++index) {
        expected.push_back(Expected{
            sample_name(index + 1, ".in"),
            sample_name(index + 1, ".out"),
            with_final_newline(package.samples[index].input),
            with_final_newline(package.samples[index].output),
        });
    }

    bool identical = true;
    std::size_t existing_sample_files = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator(samples)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() == ".in" || entry.path().extension() == ".out") {
            ++existing_sample_files;
            const std::string name = entry.path().filename().string();
            const bool known =
                std::any_of(expected.begin(), expected.end(), [&](const Expected& value) {
                    return value.input_name == name || value.output_name == name;
                });
            if (!known) {
                identical = false;
            }
        }
    }
    for (const Expected& pair : expected) {
        const fs::path input = samples / pair.input_name;
        const fs::path output = samples / pair.output_name;
        if (!fs::is_regular_file(input) || !fs::is_regular_file(output) ||
            read_file(input) != pair.input || read_file(output) != pair.output) {
            identical = false;
        }
    }

    std::size_t files_written = 0;
    if (!identical) {
        if (existing_sample_files != 0 && !force) {
            throw std::runtime_error("fetched samples differ from the current package "
                                     "(use --force to replace the complete set)");
        }

        const fs::path stage =
            package.problem.directory() / (".samples.stage." + std::to_string(::getpid()));
        const fs::path backup = package.problem.directory() / ".samples.backup";
        std::error_code ignored;
        fs::remove_all(stage, ignored);
        fs::remove_all(backup, ignored);
        fs::create_directories(stage);
        try {
            for (const Expected& pair : expected) {
                write_file(stage / pair.input_name, pair.input);
                write_file(stage / pair.output_name, pair.output);
            }
            fs::rename(samples, backup);
            try {
                fs::rename(stage, samples);
            } catch (...) {
                fs::rename(backup, samples);
                throw;
            }
            fs::remove_all(backup, ignored);
            files_written = expected.size() * 2;
        } catch (...) {
            fs::remove_all(stage, ignored);
            if (!fs::exists(samples) && fs::exists(backup)) {
                std::error_code restore_error;
                fs::rename(backup, samples, restore_error);
            }
            throw;
        }
    }

    const fs::path metadata = package.problem.directory() / "problem.json";
    write_file_atomically(metadata, "{\n"
                                    "  \"id\": " +
                                        json_quote(package.problem.id()) +
                                        ",\n"
                                        "  \"name\": " +
                                        json_quote(package.name) +
                                        ",\n"
                                        "  \"url\": " +
                                        json_quote(package.url) +
                                        ",\n"
                                        "  \"timeLimitMs\": " +
                                        std::to_string(package.time_limit_ms) +
                                        ",\n"
                                        "  \"memoryLimitMb\": " +
                                        std::to_string(package.memory_limit_mb) +
                                        "\n"
                                        "}\n");

    return ImportResult{
        package.problem,
        package.samples.size(),
        files_written,
    };
}

void serve_companion(const fs::path& root, const std::string& host, int port, bool once,
                     bool force) {
    (void)std::signal(SIGPIPE, SIG_IGN);
    const int server = listening_socket(host, port);
    std::cerr << "listening on http://" << host << ':' << port << '\n';
    do {
        const int client = ::accept(server, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR) {
                continue;
            }
            ::close(server);
            throw std::runtime_error("accept failed: " + std::string(std::strerror(errno)));
        }
        const timeval deadline{10, 0};
        (void)::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &deadline, sizeof(deadline));
        (void)::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &deadline, sizeof(deadline));
        try {
            const std::string request = receive_request(client);
            const std::size_t header_end = request.find("\r\n\r\n");
            if (!request.starts_with("POST ")) {
                throw std::runtime_error("only HTTP POST is supported");
            }
            const CompanionPackage package =
                parse_companion_package(std::string_view(request).substr(header_end + 4), root);
            const ImportResult result = import_companion_package(package, root, force);
            const std::string message = "imported " + result.problem.id() + " (" +
                                        std::to_string(result.sample_count) + " samples, " +
                                        std::to_string(result.files_written) + " files written)\n";
            send_response(client, 200, message);
            std::cout << message;
        } catch (const std::exception& error) {
            send_response(client, 400, std::string(error.what()) + "\n");
            std::cerr << "cc: " << error.what() << '\n';
        }
        ::close(client);
    } while (!once);
    ::close(server);
}

} // namespace cfx
