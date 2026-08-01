#include "judge.hpp"

#include "file.hpp"
#include "json.hpp"
#include "problem.hpp"
#include "process.hpp"
#include "runtime.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace cfx {
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

void create_text(const fs::path& path, const std::string& contents) {
    const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (descriptor < 0)
        throw std::runtime_error("cannot create " + path.string() + ": " + std::strerror(errno));
    try {
        std::size_t offset = 0;
        while (offset < contents.size()) {
            const ssize_t count = ::write(descriptor, contents.data() + offset,
                                          contents.size() - offset);
            if (count > 0)
                offset += static_cast<std::size_t>(count);
            else if (count < 0 && errno == EINTR)
                continue;
            else
                throw std::runtime_error("cannot write " + path.string());
        }
        if (::close(descriptor) != 0)
            throw std::runtime_error("cannot write " + path.string());
    } catch (...) {
        (void)::close(descriptor);
        std::error_code ignored;
        fs::remove(path, ignored);
        throw;
    }
}

void atomic_pair(const fs::path& input_path, const fs::path& output_path, const std::string& input,
                 const std::string& output) {
    fs::create_directories(input_path.parent_path());
    create_text(input_path, input);
    try {
        create_text(output_path, output);
    } catch (...) {
        std::error_code ignored;
        fs::remove(input_path, ignored);
        throw;
    }
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

    summary.build = builder_.build_problem(
        problem, BuildOptions{options.checked, options.rebuild, !options.submission_profile});
    if (!options.concise) {
        std::cout << (summary.build.compiled ? "built: " : "cached: ") << problem.id() << " ["
                  << summary.build.digest.substr(0, 12) << "]\n"
                  << "size: source " << format_bytes(summary.build.source_size) << ", binary "
                  << format_bytes(summary.build.binary_size) << '\n'
                  << "limits: time " << format_duration(limits.time_limit);
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

StressSummary Judge::stress(const Problem& problem, const StressOptions& options) const {
    if (options.iterations < 1) {
        throw std::runtime_error("stress iteration count must be positive");
    }
    const fs::path failure_directory = cfx::state_root(root_) / "failures" / problem.id();
    fs::create_directories(failure_directory);
    const fs::path ready = failure_directory / "ready";
    std::error_code ignored;
    fs::remove(ready, ignored);

    const BuildResult solution =
        builder_.build_problem(problem, BuildOptions{options.checked, options.rebuild, true});
    const BuildResult generator = builder_.build_source(
        options.generator, problem.id() + "-generator", BuildOptions{false, options.rebuild, true});
    const BuildResult brute =
        builder_.build_source(options.brute, problem.id() + "-brute",
                              BuildOptions{options.checked, options.rebuild, true});

    const fs::path input = failure_directory / "input.txt";
    const fs::path expected = failure_directory / "expected.txt";
    const fs::path actual = failure_directory / "actual.txt";
    const fs::path generator_error = failure_directory / "generator.err";
    const fs::path brute_error = failure_directory / "brute.err";
    const fs::path solution_error = failure_directory / "solution.err";

    StressSummary summary;
    for (int iteration = 0; iteration < options.iterations; ++iteration) {
        const std::uint64_t seed = options.seed + static_cast<std::uint64_t>(iteration);
        std::vector<std::string> generator_command{generator.binary.string()};
        generator_command.insert(generator_command.end(), options.generator_arguments.begin(),
                                 options.generator_arguments.end());
        generator_command.push_back(std::to_string(seed));

        const ProcessResult generated =
            run_process(generator_command, ProcessOptions{
                                               .stdout_path = input,
                                               .stderr_path = generator_error,
                                               .timeout = options.generator_timeout,
                                               .working_directory = options.generator.parent_path(),
                                           });
        if (generated.status != 0) {
            throw std::runtime_error("generator failed for seed " + std::to_string(seed));
        }

        const ProcessResult expected_run = run_process(
            {brute.binary.string()}, ProcessOptions{
                                         .stdin_path = input,
                                         .stdout_path = expected,
                                         .stderr_path = brute_error,
                                         .timeout = options.timeout,
                                         .working_directory = options.brute.parent_path(),
                                     });
        if (expected_run.status != 0) {
            throw std::runtime_error("brute force failed for seed " + std::to_string(seed));
        }
        const ProcessResult actual_run =
            run_process({solution.binary.string()},
                        ProcessOptions{
                            .stdin_path = input,
                            .stdout_path = actual,
                            .stderr_path = solution_error,
                            .timeout = options.timeout,
                            .working_directory = problem.solution_path().parent_path(),
                        });
        if (actual_run.status != 0 ||
            normalize_output(read_text(actual)) != normalize_output(read_text(expected))) {
            write_text(failure_directory / "seed.txt", std::to_string(seed) + "\n");
            write_text(ready, "failure\n");
            summary.failing_seed = seed;
            std::cout << "failed at seed " << seed << '\n'
                      << "input: " << input << '\n'
                      << "expected: " << expected << '\n'
                      << "actual: " << actual << '\n';
            return summary;
        }
        ++summary.completed;
        if (options.verbose) {
            std::cout << "seed " << seed << ": OK\n";
        } else if ((iteration + 1) % 25 == 0) {
            std::cout << (iteration + 1) << '/' << options.iterations << '\n';
        }
    }
    std::cout << summary.completed << " stress cases passed\n";
    return summary;
}

std::pair<fs::path, fs::path> Judge::promote_failure(const Problem& problem) const {
    const fs::path failure_directory = cfx::state_root(root_) / "failures" / problem.id();
    if (!fs::is_regular_file(failure_directory / "ready") ||
        !fs::is_regular_file(failure_directory / "input.txt") ||
        !fs::is_regular_file(failure_directory / "expected.txt")) {
        throw std::runtime_error("no current stress failure to promote for " + problem.id());
    }

    const fs::path directory = problem.cases_path();
    int number = 1;
    fs::path input;
    fs::path output;
    do {
        input = directory / ("stress-" + std::to_string(number) + ".in");
        output = directory / ("stress-" + std::to_string(number) + ".out");
        ++number;
    } while (fs::exists(input) || fs::exists(output));

    atomic_pair(input, output, read_text(failure_directory / "input.txt"),
                read_text(failure_directory / "expected.txt"));
    return {input, output};
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
