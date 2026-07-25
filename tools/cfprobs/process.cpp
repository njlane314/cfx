#include "process.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace cfprobs {
namespace {

[[noreturn]] void child_error(const std::string& message) {
    const std::string line = "probs: " + message + ": " + std::strerror(errno) + "\n";
    (void)::write(STDERR_FILENO, line.data(), line.size());
    _exit(127);
}

void redirect_to(const std::filesystem::path& path, int target, int flags) {
    const int descriptor = ::open(path.c_str(), flags, 0666);
    if (descriptor < 0) {
        child_error("cannot open " + path.string());
    }
    if (::dup2(descriptor, target) < 0) {
        child_error("cannot redirect " + path.string());
    }
    ::close(descriptor);
}

std::vector<char*> exec_arguments(const std::vector<std::string>& arguments) {
    std::vector<char*> result;
    result.reserve(arguments.size() + 1);
    for (const std::string& argument : arguments) {
        result.push_back(const_cast<char*>(argument.c_str()));
    }
    result.push_back(nullptr);
    return result;
}

int exit_status(int wait_status) {
    if (WIFEXITED(wait_status)) {
        return WEXITSTATUS(wait_status);
    }
    if (WIFSIGNALED(wait_status)) {
        return 128 + WTERMSIG(wait_status);
    }
    return 127;
}

void wait_for_pid(pid_t pid, int& wait_status) {
    while (::waitpid(pid, &wait_status, 0) < 0) {
        if (errno != EINTR) {
            throw std::runtime_error("waitpid failed: " + std::string(std::strerror(errno)));
        }
    }
}

} // namespace

ProcessResult run_process(const std::vector<std::string>& arguments,
                          const ProcessOptions& options) {
    if (arguments.empty()) {
        throw std::invalid_argument("cannot run an empty command");
    }

    const auto started = std::chrono::steady_clock::now();
    const pid_t pid = ::fork();
    if (pid < 0) {
        throw std::runtime_error("fork failed: " + std::string(std::strerror(errno)));
    }
    if (pid == 0) {
        (void)::setpgid(0, 0);
        if (options.working_directory && ::chdir(options.working_directory->c_str()) != 0) {
            child_error("cannot enter " + options.working_directory->string());
        }
        if (options.stdin_path) {
            redirect_to(*options.stdin_path, STDIN_FILENO, O_RDONLY);
        }
        if (options.stdout_path) {
            redirect_to(*options.stdout_path, STDOUT_FILENO, O_WRONLY | O_CREAT | O_TRUNC);
        }
        if (options.stderr_path) {
            redirect_to(*options.stderr_path, STDERR_FILENO, O_WRONLY | O_CREAT | O_TRUNC);
        }

        std::vector<char*> argv = exec_arguments(arguments);
        ::execvp(argv.front(), argv.data());
        child_error("cannot execute " + arguments.front());
    }

    (void)::setpgid(pid, pid);
    int wait_status = 0;
    bool timed_out = false;
    if (!options.timeout) {
        wait_for_pid(pid, wait_status);
    } else {
        while (true) {
            const pid_t result = ::waitpid(pid, &wait_status, WNOHANG);
            if (result == pid) {
                break;
            }
            if (result < 0 && errno != EINTR) {
                throw std::runtime_error("waitpid failed: " + std::string(std::strerror(errno)));
            }
            if (std::chrono::steady_clock::now() - started >= *options.timeout) {
                timed_out = true;
                (void)::kill(-pid, SIGKILL);
                (void)::kill(pid, SIGKILL);
                wait_for_pid(pid, wait_status);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    return ProcessResult{
        timed_out ? 124 : exit_status(wait_status),
        timed_out,
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                              started),
    };
}

CaptureResult capture_process(const std::vector<std::string>& arguments) {
    if (arguments.empty()) {
        throw std::invalid_argument("cannot run an empty command");
    }

    int descriptors[2]{};
    if (::pipe(descriptors) != 0) {
        throw std::runtime_error("pipe failed: " + std::string(std::strerror(errno)));
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(descriptors[0]);
        ::close(descriptors[1]);
        throw std::runtime_error("fork failed: " + std::string(std::strerror(errno)));
    }
    if (pid == 0) {
        ::close(descriptors[0]);
        if (::dup2(descriptors[1], STDOUT_FILENO) < 0 ||
            ::dup2(descriptors[1], STDERR_FILENO) < 0) {
            child_error("cannot capture command output");
        }
        ::close(descriptors[1]);
        std::vector<char*> argv = exec_arguments(arguments);
        ::execvp(argv.front(), argv.data());
        child_error("cannot execute " + arguments.front());
    }

    ::close(descriptors[1]);
    std::string output;
    char buffer[4096];
    while (true) {
        const ssize_t count = ::read(descriptors[0], buffer, sizeof(buffer));
        if (count > 0) {
            output.append(buffer, static_cast<std::size_t>(count));
        } else if (count == 0) {
            break;
        } else if (errno != EINTR) {
            ::close(descriptors[0]);
            throw std::runtime_error("read failed: " + std::string(std::strerror(errno)));
        }
    }
    ::close(descriptors[0]);

    int wait_status = 0;
    wait_for_pid(pid, wait_status);
    return CaptureResult{exit_status(wait_status), std::move(output)};
}

std::vector<std::string> split_command_words(const std::string& value) {
    std::vector<std::string> words;
    std::string word;
    char quote = '\0';
    bool escaped = false;

    for (const char character : value) {
        if (escaped) {
            word.push_back(character);
            escaped = false;
        } else if (character == '\\' && quote != '\'') {
            escaped = true;
        } else if (quote != '\0') {
            if (character == quote) {
                quote = '\0';
            } else {
                word.push_back(character);
            }
        } else if (character == '\'' || character == '"') {
            quote = character;
        } else if (character == ' ' || character == '\t' || character == '\n') {
            if (!word.empty()) {
                words.push_back(std::move(word));
                word.clear();
            }
        } else {
            word.push_back(character);
        }
    }

    if (escaped || quote != '\0') {
        throw std::invalid_argument("unclosed quote or escape in command flags");
    }
    if (!word.empty()) {
        words.push_back(std::move(word));
    }
    return words;
}

} // namespace cfprobs
