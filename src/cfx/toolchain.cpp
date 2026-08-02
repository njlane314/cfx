#include "cfx.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

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

namespace {

std::string environment(const char* name, const std::string& fallback = {}) {
    const char* value = std::getenv(name);
    return value == nullptr ? fallback : std::string(value);
}

std::vector<std::string> compiler_command() {
    std::vector<std::string> command = split_command_words(environment("CXX", "c++"));
    if (command.empty()) {
        throw std::runtime_error("CXX names no compiler");
    }
    return command;
}

std::vector<std::string> compile_flags(const std::string& standard, bool checked, bool local) {
    std::vector<std::string> flags{
        "-std=" + standard, "-pipe", "-Wall", "-Wextra", "-Wshadow", "-Wformat=2", "-pthread",
    };
    if (local) {
        flags.push_back("-DLOCAL");
    }
    if (checked) {
        const std::vector<std::string> diagnostic{
            "-O1",
            "-g",
            "-pedantic",
            "-Wconversion",
            "-Wsign-conversion",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-fno-sanitize-recover=all",
        };
        flags.insert(flags.end(), diagnostic.begin(), diagnostic.end());
    } else {
        flags.push_back("-O2");
    }

    const std::string custom = environment("CFX_CXXFLAGS");
    if (!custom.empty()) {
        std::vector<std::string> additions = split_command_words(custom);
        flags.insert(flags.end(), additions.begin(), additions.end());
    }
    if (!local) {
        flags.push_back("-ULOCAL");
        flags.push_back("-DONLINE_JUDGE");
    }
    flags.push_back("-UPEEK_COMPILED");
    flags.push_back(local ? "-DPEEK_COMPILED=1" : "-DPEEK_COMPILED=0");
    return flags;
}

std::string join_fingerprint(const std::string& source, const std::vector<std::string>& compiler,
                             const std::vector<std::string>& flags) {
    std::string fingerprint = source;
    const auto append = [&](const std::string& value) {
        fingerprint.push_back('\0');
        fingerprint.append(value);
    };
    for (const std::string& part : compiler) {
        append(part);
    }
    std::vector<std::string> version_command = compiler;
    version_command.push_back("--version");
    const CaptureResult version = capture_process(version_command);
    append(std::to_string(version.status));
    append(version.output);
    for (const std::string& flag : flags) {
        append(flag);
    }
    return fingerprint;
}

} // namespace

Builder::Builder(std::filesystem::path root)
    : root_(std::filesystem::weakly_canonical(std::move(root))) {}

std::string Builder::bundled_source(const std::filesystem::path& source) const {
    return bundle(source, root_);
}

BuildResult Builder::build_problem(const Problem& problem, const BuildOptions& options) const {
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(problem.solution_path());
    if (!std::filesystem::is_regular_file(canonical)) {
        throw std::runtime_error("source not found: " + problem.solution_path().string());
    }

    const std::string prepared = bundled_source(canonical);
    const std::vector<std::string> compiler = compiler_command();
    const std::string standard = configured_standard();
    const std::vector<std::string> flags = compile_flags(standard, options.checked, options.local);
    const std::string digest = content_digest(join_fingerprint(prepared, compiler, flags));

    const std::filesystem::path cache = cfx::state_root(root_) / "build" / problem.id();
    std::filesystem::create_directories(cache);

    const std::filesystem::path bundled = cache / (digest + ".cpp");
    const std::filesystem::path binary = cache / digest;
    write_atomic(bundled, prepared);

    if (!std::filesystem::is_regular_file(binary)) {
        const std::filesystem::path temporary =
            cache / ("." + digest + "." + std::to_string(::getpid()) + ".tmp");
        std::vector<std::string> command = compiler;
        command.insert(command.end(), flags.begin(), flags.end());
        command.push_back(bundled.string());
        command.push_back("-o");
        command.push_back(temporary.string());

        const ProcessResult result = run_process(command);
        if (result.status != 0) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw std::runtime_error("compile failed: " + canonical.string());
        }
        std::error_code error;
        std::filesystem::rename(temporary, binary, error);
        if (error) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw std::runtime_error("cannot install cached binary: " + error.message());
        }
    }

    return BuildResult{binary, bundled};
}

std::string format_bytes(std::uintmax_t bytes) {
    std::ostringstream stream;
    if (bytes < 1024) {
        stream << bytes << 'B';
    } else if (bytes < 1024 * 1024) {
        stream << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / 1024.0
               << "KiB";
    } else {
        stream << std::fixed << std::setprecision(1)
               << static_cast<double>(bytes) / (1024.0 * 1024.0) << "MiB";
    }
    return stream.str();
}

std::string configured_standard() {
    return environment("CFX_STD", "c++20");
}

namespace {

namespace fs = std::filesystem;

constexpr auto kFallbackTimeLimit = std::chrono::milliseconds(5000);
constexpr std::uint64_t kMebibyte = 1024U * 1024U;

struct Case {
    std::string name;
    fs::path input;
    fs::path expected;
};

enum class CaseVerdict {
    time_limit_exceeded,
    memory_limit_exceeded,
    output_limit_exceeded,
    runtime_error,
};

std::string verdict_name(CaseVerdict verdict);

bool natural_less(const std::string& left, const std::string& right) {
    std::size_t a = 0;
    std::size_t b = 0;
    while (a < left.size() && b < right.size()) {
        const bool a_digit = std::isdigit(static_cast<unsigned char>(left[a])) != 0;
        const bool b_digit = std::isdigit(static_cast<unsigned char>(right[b])) != 0;
        if (a_digit && b_digit) {
            std::size_t a_end = a;
            std::size_t b_end = b;
            while (a_end < left.size() && std::isdigit(static_cast<unsigned char>(left[a_end])))
                ++a_end;
            while (b_end < right.size() && std::isdigit(static_cast<unsigned char>(right[b_end])))
                ++b_end;
            const std::string_view a_number(left.data() + a, a_end - a);
            const std::string_view b_number(right.data() + b, b_end - b);
            const auto a_zero = a_number.find_first_not_of('0');
            const auto b_zero = b_number.find_first_not_of('0');
            const std::string_view a_trimmed = a_zero == std::string_view::npos
                                                   ? a_number.substr(a_number.size() - 1)
                                                   : a_number.substr(a_zero);
            const std::string_view b_trimmed = b_zero == std::string_view::npos
                                                   ? b_number.substr(b_number.size() - 1)
                                                   : b_number.substr(b_zero);
            if (a_trimmed.size() != b_trimmed.size())
                return a_trimmed.size() < b_trimmed.size();
            if (a_trimmed != b_trimmed)
                return a_trimmed < b_trimmed;
            a = a_end;
            b = b_end;
        } else {
            const char ac = static_cast<char>(std::tolower(static_cast<unsigned char>(left[a])));
            const char bc = static_cast<char>(std::tolower(static_cast<unsigned char>(right[b])));
            if (ac != bc)
                return ac < bc;
            ++a;
            ++b;
        }
    }
    return left.size() < right.size();
}

std::vector<Case> cases_for(const Problem& problem) {
    std::vector<Case> result;
    for (const fs::path& directory : problem.test_directories()) {
        if (!fs::is_directory(directory)) {
            continue;
        }
        std::vector<Case> directory_cases;
        for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            if (entry.path().extension() == ".out") {
                fs::path input = entry.path();
                input.replace_extension(".in");
                if (!fs::is_regular_file(input)) {
                    throw std::runtime_error("orphan expected output: " + entry.path().string());
                }
                continue;
            }
            if (entry.path().extension() != ".in") {
                continue;
            }
            fs::path answer = entry.path();
            answer.replace_extension(".out");
            directory_cases.push_back(Case{
                directory.filename().string() + "/" + entry.path().filename().string(),
                entry.path(),
                answer,
            });
        }
        std::sort(directory_cases.begin(), directory_cases.end(),
                  [](const Case& left, const Case& right) {
                      return natural_less(left.name, right.name);
                  });
        result.insert(result.end(), directory_cases.begin(), directory_cases.end());
    }
    return result;
}

std::string preview(const std::string& value);

void show_stream_if_nonempty(const std::string& label, const fs::path& path, std::ostream& output) {
    if (fs::is_regular_file(path) && fs::file_size(path) != 0) {
        const std::string contents = preview(read_text(path));
        output << label << ":\n" << contents;
        if (contents.back() != '\n') {
            output << '\n';
        }
    }
}

std::string preview(const std::string& value) {
    constexpr std::size_t limit = 4096;
    if (value.size() <= limit) {
        return value;
    }
    return value.substr(0, limit) + "\n... output truncated ...\n";
}

std::optional<std::uint64_t> metadata_integer(const Json& document, std::string_view name,
                                              std::uint64_t maximum,
                                              const fs::path& metadata_path) {
    const Json* field = document.find(name);
    if (field == nullptr) {
        return std::nullopt;
    }
    if (!field->is_number()) {
        throw std::runtime_error(metadata_path.string() + ": " + std::string(name) +
                                 " must be a number");
    }
    const double value = field->number();
    if (value < 1.0 || value > static_cast<double>(maximum) || std::floor(value) != value) {
        throw std::runtime_error(metadata_path.string() + ": " + std::string(name) +
                                 " must be a positive integer");
    }
    return static_cast<std::uint64_t>(value);
}

std::string resource_usage(const ProcessResult& result, const ProblemLimits& limits) {
    std::string description = format_duration(result.cpu_time) + " CPU, " +
                              format_duration(result.elapsed) + " wall / " +
                              format_duration(limits.time_limit);
    if (result.peak_memory_bytes != 0) {
        description += ", " + format_bytes(result.peak_memory_bytes);
        if (limits.memory_limit_bytes) {
            description += " / " + format_bytes(*limits.memory_limit_bytes);
        }
    }
    return description;
}

std::string runtime_detail(const ProcessResult& result) {
    if (result.signal != 0) {
        const char* description = ::strsignal(result.signal);
        return "signal " + std::to_string(result.signal) +
               (description == nullptr ? std::string{} : " (" + std::string(description) + ")");
    }
    return "exit " + std::to_string(result.exit_code);
}

CaseVerdict process_verdict(const ProcessResult& result) {
    if (result.timed_out) {
        return CaseVerdict::time_limit_exceeded;
    }
    if (result.memory_limit_exceeded) {
        return CaseVerdict::memory_limit_exceeded;
    }
    if (result.output_limit_exceeded) {
        return CaseVerdict::output_limit_exceeded;
    }
    return CaseVerdict::runtime_error;
}

} // namespace

ProblemLimits load_problem_limits(const Problem& problem) {
    ProblemLimits limits;
    const fs::path metadata_path = problem.metadata_path();
    if (!fs::is_regular_file(metadata_path)) {
        return limits;
    }

    Json document;
    try {
        document = parse_json(read_text(metadata_path));
    } catch (const JsonError& error) {
        throw std::runtime_error(metadata_path.string() + ": " + error.what());
    }
    if (!document.is_object()) {
        throw std::runtime_error(metadata_path.string() + ": metadata must be a JSON object");
    }
    if (const Json* id = document.find("id"); id != nullptr) {
        if (!id->is_string() || id->string() != problem.id()) {
            throw std::runtime_error(metadata_path.string() + ": problem id does not match " +
                                     problem.id());
        }
    }

    if (const auto milliseconds =
            metadata_integer(document, "timeLimitMs", 86'400'000U, metadata_path)) {
        limits.time_limit = std::chrono::milliseconds(*milliseconds);
        limits.time_from_metadata = true;
    }
    if (const auto mebibytes =
            metadata_integer(document, "memoryLimitMb", 1'048'576U, metadata_path)) {
        limits.memory_limit_bytes = *mebibytes * kMebibyte;
        limits.memory_from_metadata = true;
    }
    return limits;
}

Judge::Judge(fs::path root) : root_(fs::weakly_canonical(std::move(root))), builder_(root_) {}

TestSummary Judge::test(const Problem& problem, const TestOptions& options) const {
    TestSummary summary;
    ProblemLimits limits = load_problem_limits(problem);
    if (options.checked && !options.timeout) {
        if (limits.time_limit < kFallbackTimeLimit) {
            limits.time_limit = kFallbackTimeLimit;
            limits.time_from_metadata = false;
        }
    }
    if (options.checked && !options.memory_limit_bytes) {
        limits.memory_limit_bytes.reset();
        limits.memory_from_metadata = false;
    }
    if (options.timeout) {
        limits.time_limit = *options.timeout;
        limits.time_from_metadata = false;
    }
    if (options.memory_limit_bytes) {
        limits.memory_limit_bytes = options.memory_limit_bytes;
        limits.memory_from_metadata = false;
    }

    summary.build =
        builder_.build_problem(problem, BuildOptions{options.checked, !options.submission_profile});
    if (!options.concise) {
        std::cout << "limits: time " << format_duration(limits.time_limit);
        if (!limits.time_from_metadata && !options.timeout) {
            std::cout << " (fallback)";
        }
        std::cout << ", memory "
                  << (limits.memory_limit_bytes
                          ? format_bytes(*limits.memory_limit_bytes)
                          : std::string("unlimited"))
                  << ", output "
                  << (options.output_limit_bytes ? format_bytes(*options.output_limit_bytes)
                                                 : std::string("unlimited"))
                  << '\n';
        if (options.checked) {
            std::cout << "note: checked-build resource use is not Codeforces-comparable\n";
        }
    }

    const std::vector<Case> cases = cases_for(problem);
    if (cases.empty()) {
        std::cout << "no tests found\n";
        return summary;
    }

    const fs::path run_directory = cfx::state_root(root_) / "runs" / problem.id();
    fs::create_directories(run_directory);

    int number = 0;
    std::chrono::milliseconds max_wall_time{0};
    std::chrono::milliseconds max_cpu_time{0};
    std::uint64_t peak_memory_bytes = 0;
    for (const Case& test_case : cases) {
        ++number;
        ++summary.total;
        const fs::path actual_path = run_directory / ("actual-" + std::to_string(number) + ".txt");
        const fs::path error_path = run_directory / ("error-" + std::to_string(number) + ".txt");

        if (!options.concise) {
            std::cout << "==> " << test_case.name << '\n';
        }
        if (!fs::is_regular_file(test_case.expected)) {
            std::cout << (options.concise ? test_case.name + ": " : "")
                      << "missing expected output: " << test_case.input.stem().string() << ".out\n";
            continue;
        }
        const ProcessResult result =
            run_process({summary.build.binary.string()},
                        ProcessOptions{
                            .stdin_path = test_case.input,
                            .stdout_path = actual_path,
                            .stderr_path = error_path,
                            .timeout = limits.time_limit,
                            .working_directory = problem.solution_path().parent_path(),
                            .memory_limit_bytes = limits.memory_limit_bytes,
                            .output_limit_bytes = options.output_limit_bytes,
                        });
        max_wall_time = std::max(max_wall_time, result.elapsed);
        max_cpu_time = std::max(max_cpu_time, result.cpu_time);
        peak_memory_bytes = std::max(peak_memory_bytes, result.peak_memory_bytes);
        if (result.status != 0) {
            const CaseVerdict verdict = process_verdict(result);
            std::cout << (options.concise ? test_case.name + ": " : "") << verdict_name(verdict)
                      << " (";
            if (verdict == CaseVerdict::runtime_error) {
                std::cout << runtime_detail(result) << ", ";
            }
            std::cout << resource_usage(result, limits) << ")\n";
            show_stream_if_nonempty("stderr", error_path, std::cerr);
            continue;
        }

        show_stream_if_nonempty("stderr", error_path, std::cerr);
        const std::string actual = read_text(actual_path);
        const std::string expected = read_text(test_case.expected);
        if (normalize_output(actual) == normalize_output(expected)) {
            if (!options.concise) {
                std::cout << "OK (" << resource_usage(result, limits) << ")\n";
            }
            ++summary.passed;
        } else {
            std::cout << (options.concise ? test_case.name + ": " : "") << "WA ("
                      << resource_usage(result, limits) << ")\n"
                      << "expected:\n"
                      << preview(expected)
                      << (expected.empty() || expected.back() == '\n' ? "" : "\n") << "actual:\n"
                      << preview(actual) << (actual.empty() || actual.back() == '\n' ? "" : "\n");
        }
    }
    if (options.concise) {
        std::cout << summary.passed << '/' << summary.total << " tests passed";
    } else {
        std::cout << summary.passed << '/' << summary.total << " passed";
    }
    if (summary.total != 0) {
        std::cout << "; max " << format_duration(max_cpu_time) << " CPU, "
                  << format_duration(max_wall_time) << " wall";
        if (peak_memory_bytes != 0) {
            std::cout << ", " << format_bytes(peak_memory_bytes);
        }
    }
    std::cout << '\n';
    return summary;
}

std::string normalize_output(const std::string& output) {
    std::string normalized;
    std::size_t start = 0;
    while (start <= output.size()) {
        const std::size_t newline = output.find('\n', start);
        const std::size_t end = newline == std::string::npos ? output.size() : newline;
        std::string_view line(output.data() + start, end - start);
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())) != 0) {
            line.remove_suffix(1);
        }
        if (!line.empty()) {
            if (!normalized.empty()) {
                normalized.push_back('\n');
            }
            normalized.append(line);
        }
        if (newline == std::string::npos) {
            break;
        }
        start = newline + 1;
    }
    return normalized;
}

std::string format_duration(std::chrono::milliseconds duration) {
    if (duration.count() < 1000) {
        return std::to_string(duration.count()) + "ms";
    }
    const long long seconds = duration.count() / 1000;
    const long long milliseconds = duration.count() % 1000;
    std::string fraction = std::to_string(1000 + milliseconds).substr(1);
    return std::to_string(seconds) + "." + fraction + "s";
}

namespace {

std::string verdict_name(CaseVerdict verdict) {
    switch (verdict) {
    case CaseVerdict::time_limit_exceeded:
        return "TLE";
    case CaseVerdict::memory_limit_exceeded:
        return "MLE";
    case CaseVerdict::output_limit_exceeded:
        return "OLE";
    case CaseVerdict::runtime_error:
        return "RE";
    }
    return "unknown";
}

} // namespace

} // namespace cfx
