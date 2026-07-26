#include "process.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#ifdef __APPLE__
#include <libproc.h>
#endif

namespace cfx {
namespace {

constexpr std::array<int, 3> kForwardedSignals{SIGINT, SIGTERM, SIGHUP};
volatile sig_atomic_t active_process_group = 0;
volatile sig_atomic_t forwarded_signal = 0;

void forward_process_signal(int signal) {
    forwarded_signal = signal;
    const auto group = static_cast<pid_t>(active_process_group);
    if (group > 0) {
        (void)::kill(-group, signal);
    }
}

[[noreturn]] void child_error(const std::string& message) {
    const std::string line = "cfx: " + message + ": " + std::strerror(errno) + "\n";
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

int exit_code(int wait_status) {
    if (WIFEXITED(wait_status)) {
        return WEXITSTATUS(wait_status);
    }
    return -1;
}

int exit_signal(int wait_status) {
    if (WIFSIGNALED(wait_status)) {
        return WTERMSIG(wait_status);
    }
    return 0;
}

int exit_status(int wait_status) {
    const int code = exit_code(wait_status);
    if (code >= 0) {
        return code;
    }
    const int signal = exit_signal(wait_status);
    return signal == 0 ? 127 : 128 + signal;
}

void wait_for_pid(pid_t pid, int& wait_status, rusage* usage = nullptr) {
    rusage ignored{};
    while (::wait4(pid, &wait_status, 0, usage == nullptr ? &ignored : usage) < 0) {
        if (errno != EINTR) {
            throw std::runtime_error("wait4 failed: " + std::string(std::strerror(errno)));
        }
    }
}

class SignalForwarder {
  public:
    explicit SignalForwarder(pid_t group) : group_(group) {
        struct sigaction action {};
        action.sa_handler = forward_process_signal;
        sigemptyset(&action.sa_mask);
        action.sa_flags = 0;

        active_process_group = static_cast<sig_atomic_t>(group_);
        forwarded_signal = 0;
        for (; installed_ < kForwardedSignals.size(); ++installed_) {
            if (::sigaction(kForwardedSignals[installed_], &action,
                            &previous_[installed_]) != 0) {
                const int error = errno;
                restore();
                (void)::kill(-group_, SIGKILL);
                (void)::kill(group_, SIGKILL);
                int ignored = 0;
                try {
                    wait_for_pid(group_, ignored);
                } catch (...) {
                }
                throw std::runtime_error("sigaction failed: " +
                                         std::string(std::strerror(error)));
            }
        }
    }

    SignalForwarder(const SignalForwarder&) = delete;
    SignalForwarder& operator=(const SignalForwarder&) = delete;

    ~SignalForwarder() {
        restore();
    }

    [[nodiscard]] int received() const noexcept {
        return static_cast<int>(forwarded_signal);
    }

    void restore() noexcept {
        if (restored_) {
            return;
        }
        sigset_t blocked{};
        sigset_t previous_mask{};
        sigemptyset(&blocked);
        for (const int signal : kForwardedSignals) {
            sigaddset(&blocked, signal);
        }
        const bool mask_changed = ::sigprocmask(SIG_BLOCK, &blocked, &previous_mask) == 0;
        active_process_group = 0;
        while (installed_ > 0) {
            --installed_;
            (void)::sigaction(kForwardedSignals[installed_], &previous_[installed_], nullptr);
        }
        if (mask_changed) {
            (void)::sigprocmask(SIG_SETMASK, &previous_mask, nullptr);
        }
        restored_ = true;
    }

  private:
    pid_t group_;
    std::array<struct sigaction, kForwardedSignals.size()> previous_{};
    std::size_t installed_ = 0;
    bool restored_ = false;
};

std::chrono::milliseconds timeval_duration(const timeval& value) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::seconds(value.tv_sec) + std::chrono::microseconds(value.tv_usec));
}

std::uint64_t peak_memory(const rusage& usage) {
    if (usage.ru_maxrss <= 0) {
        return 0;
    }
#ifdef __APPLE__
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024U;
#endif
}

std::optional<std::uint64_t> current_memory(pid_t pid) {
#ifdef __APPLE__
    rusage_info_v4 usage{};
    if (::proc_pid_rusage(pid, RUSAGE_INFO_V4,
                          reinterpret_cast<rusage_info_t*>(&usage)) != 0) {
        return std::nullopt;
    }
    return std::max(usage.ri_resident_size, usage.ri_phys_footprint);
#elif defined(__linux__)
    std::ifstream input("/proc/" + std::to_string(pid) + "/status");
    std::string line;
    while (std::getline(input, line)) {
        if (line.starts_with("VmRSS:") || line.starts_with("VmHWM:")) {
            std::istringstream fields(line);
            std::string key;
            std::uint64_t value = 0;
            std::string unit;
            if (fields >> key >> value >> unit) {
                return unit == "kB" ? value * 1024U : value;
            }
        }
    }
    return std::nullopt;
#else
    (void)pid;
    return std::nullopt;
#endif
}

void set_resource_limit(int resource, std::uint64_t value, const std::string& name) {
    if (value == 0 || value > static_cast<std::uint64_t>(std::numeric_limits<rlim_t>::max())) {
        child_error("invalid " + name + " limit");
    }
    rlimit current{};
    if (::getrlimit(resource, &current) != 0) {
        child_error("cannot inspect " + name + " limit");
    }
    const rlim_t requested = static_cast<rlim_t>(value);
    const rlim_t effective = current.rlim_max == RLIM_INFINITY
                                 ? requested
                                 : std::min(requested, current.rlim_max);
    const rlimit limit{effective, effective};
    if (::setrlimit(resource, &limit) != 0) {
        child_error("cannot set " + name + " limit");
    }
}

void apply_process_limits(const ProcessOptions& options) {
    const rlimit no_core{0, 0};
    if (::setrlimit(RLIMIT_CORE, &no_core) != 0) {
        child_error("cannot disable core dumps");
    }
    if (options.output_limit_bytes) {
        set_resource_limit(RLIMIT_FSIZE, *options.output_limit_bytes, "output");
    }
    if (options.timeout) {
        const auto milliseconds = options.timeout->count();
        const std::uint64_t seconds =
            std::max<std::uint64_t>(1, (static_cast<std::uint64_t>(milliseconds) + 999U) / 1000U);
        rlimit current{};
        if (::getrlimit(RLIMIT_CPU, &current) != 0) {
            child_error("cannot inspect CPU time limit");
        }
        rlim_t soft = static_cast<rlim_t>(seconds);
        rlim_t hard = seconds == std::numeric_limits<rlim_t>::max()
                          ? soft
                          : static_cast<rlim_t>(seconds + 1U);
        if (current.rlim_max != RLIM_INFINITY) {
            hard = std::min(hard, current.rlim_max);
            soft = std::min(soft, hard);
        }
        const rlimit limit{soft, hard};
        if (::setrlimit(RLIMIT_CPU, &limit) != 0) {
            child_error("cannot set CPU time limit");
        }
    }
}

void set_close_on_exec(int descriptor) {
    const int flags = ::fcntl(descriptor, F_GETFD);
    if (flags < 0 || ::fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) < 0) {
        throw std::runtime_error("fcntl failed: " + std::string(std::strerror(errno)));
    }
}

void move_above_standard_streams(int& descriptor) {
    if (descriptor > STDERR_FILENO) {
        return;
    }
    const int moved = ::fcntl(descriptor, F_DUPFD, STDERR_FILENO + 1);
    if (moved < 0) {
        throw std::runtime_error("fcntl failed: " + std::string(std::strerror(errno)));
    }
    ::close(descriptor);
    descriptor = moved;
}

[[noreturn]] void report_launch_error(int descriptor, int error) {
    const char* data = reinterpret_cast<const char*>(&error);
    std::size_t written = 0;
    while (written < sizeof(error)) {
        const ssize_t count = ::write(descriptor, data + written, sizeof(error) - written);
        if (count > 0) {
            written += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    _exit(127);
}

void redirect_standard_streams(int error_descriptor) {
    const int null_descriptor = ::open("/dev/null", O_RDWR);
    if (null_descriptor < 0) {
        report_launch_error(error_descriptor, errno);
    }
    for (int target = STDIN_FILENO; target <= STDERR_FILENO; ++target) {
        if (::dup2(null_descriptor, target) < 0) {
            const int error = errno;
            ::close(null_descriptor);
            report_launch_error(error_descriptor, error);
        }
    }
    if (null_descriptor > STDERR_FILENO) {
        ::close(null_descriptor);
    }
}

} // namespace

ProcessResult run_process(const std::vector<std::string>& arguments,
                          const ProcessOptions& options) {
    if (arguments.empty()) {
        throw std::invalid_argument("cannot run an empty command");
    }
    if (options.timeout && *options.timeout <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("process timeout must be positive");
    }
    if (options.memory_limit_bytes && *options.memory_limit_bytes == 0) {
        throw std::invalid_argument("process memory limit must be positive");
    }
    if (options.output_limit_bytes && *options.output_limit_bytes == 0) {
        throw std::invalid_argument("process output limit must be positive");
    }

    const auto started = std::chrono::steady_clock::now();
    const pid_t pid = ::fork();
    if (pid < 0) {
        throw std::runtime_error("fork failed: " + std::string(std::strerror(errno)));
    }
    if (pid == 0) {
        (void)::setpgid(0, 0);
        apply_process_limits(options);
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
    SignalForwarder signal_forwarder(pid);
    int wait_status = 0;
    rusage usage{};
    bool timed_out = false;
    bool memory_limit_exceeded = false;
    std::uint64_t sampled_peak_memory = 0;
    while (true) {
        const pid_t result = ::wait4(pid, &wait_status, WNOHANG, &usage);
        if (result == pid) {
            break;
        }
        if (result < 0 && errno != EINTR) {
            throw std::runtime_error("wait4 failed: " + std::string(std::strerror(errno)));
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - started >= std::chrono::milliseconds(2)) {
            if (const auto memory = current_memory(pid)) {
                sampled_peak_memory = std::max(sampled_peak_memory, *memory);
                if (options.memory_limit_bytes && *memory > *options.memory_limit_bytes) {
                    memory_limit_exceeded = true;
                }
            }
        }
        const bool interrupted = signal_forwarder.received() != 0;
        if (interrupted || memory_limit_exceeded ||
            (options.timeout && now - started >= *options.timeout)) {
            timed_out = !interrupted && !memory_limit_exceeded;
            (void)::kill(-pid, SIGKILL);
            (void)::kill(pid, SIGKILL);
            wait_for_pid(pid, wait_status, &usage);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // The group leader may exit after spawning descendants. Always close the
    // remaining process group so a successful-looking run cannot leak work
    // into later cases or continue writing its redirected output.
    (void)::kill(-pid, SIGKILL);

    const int interruption = signal_forwarder.received();
    signal_forwarder.restore();
    if (interruption != 0) {
        (void)::raise(interruption);
    }

    const int signal = exit_signal(wait_status);
    if (options.timeout && signal == SIGXCPU) {
        timed_out = true;
    }
    const bool output_limit_exceeded =
        options.output_limit_bytes.has_value() && signal == SIGXFSZ;
    const std::uint64_t measured_peak_memory =
        std::max(sampled_peak_memory, peak_memory(usage));
    if (options.memory_limit_bytes && measured_peak_memory > *options.memory_limit_bytes) {
        memory_limit_exceeded = true;
    }
    int status = exit_status(wait_status);
    if (timed_out) {
        status = 124;
    } else if (memory_limit_exceeded) {
        status = 125;
    }

    return ProcessResult{
        status,
        exit_code(wait_status),
        signal,
        timed_out,
        memory_limit_exceeded,
        output_limit_exceeded,
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                              started),
        timeval_duration(usage.ru_utime) + timeval_duration(usage.ru_stime),
        measured_peak_memory,
    };
}

void launch_detached_process(const std::vector<std::string>& arguments) {
    if (arguments.empty()) {
        throw std::invalid_argument("cannot launch an empty command");
    }
    std::vector<char*> argv = exec_arguments(arguments);

    int errors[2]{};
    if (::pipe(errors) != 0) {
        throw std::runtime_error("pipe failed: " + std::string(std::strerror(errno)));
    }
    try {
        move_above_standard_streams(errors[0]);
        move_above_standard_streams(errors[1]);
        set_close_on_exec(errors[1]);
    } catch (...) {
        ::close(errors[0]);
        ::close(errors[1]);
        throw;
    }

    const pid_t child = ::fork();
    if (child < 0) {
        const int error = errno;
        ::close(errors[0]);
        ::close(errors[1]);
        throw std::runtime_error("fork failed: " + std::string(std::strerror(error)));
    }
    if (child == 0) {
        ::close(errors[0]);
        if (::setsid() < 0) {
            report_launch_error(errors[1], errno);
        }
        const pid_t detached = ::fork();
        if (detached < 0) {
            report_launch_error(errors[1], errno);
        }
        if (detached > 0) {
            ::close(errors[1]);
            _exit(0);
        }

        redirect_standard_streams(errors[1]);
        ::execvp(argv.front(), argv.data());
        report_launch_error(errors[1], errno);
    }

    ::close(errors[1]);
    int wait_status = 0;
    try {
        wait_for_pid(child, wait_status);
    } catch (...) {
        ::close(errors[0]);
        throw;
    }

    int launch_error = 0;
    std::size_t received = 0;
    while (received < sizeof(launch_error)) {
        const ssize_t count = ::read(errors[0], reinterpret_cast<char*>(&launch_error) + received,
                                     sizeof(launch_error) - received);
        if (count > 0) {
            received += static_cast<std::size_t>(count);
        } else if (count == 0) {
            break;
        } else if (errno != EINTR) {
            const int error = errno;
            ::close(errors[0]);
            throw std::runtime_error("read failed: " + std::string(std::strerror(error)));
        }
    }
    ::close(errors[0]);

    if (received != 0) {
        if (received != sizeof(launch_error)) {
            throw std::runtime_error("detached process failed before launch");
        }
        throw std::runtime_error("cannot execute " + arguments.front() + ": " +
                                 std::strerror(launch_error));
    }
    if (!WIFEXITED(wait_status) || WEXITSTATUS(wait_status) != 0) {
        throw std::runtime_error("detached process failed before launch");
    }
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

} // namespace cfx
