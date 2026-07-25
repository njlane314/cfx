#include "judge.hpp"

#include "problem.hpp"
#include "process.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace cfprobs {
namespace {

namespace fs = std::filesystem;

struct Case {
    std::string name;
    fs::path input;
    std::optional<fs::path> expected;
};

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

bool natural_less(const std::string& left, const std::string& right) {
    std::size_t a = 0;
    std::size_t b = 0;
    while (a < left.size() && b < right.size()) {
        const bool a_digit = std::isdigit(static_cast<unsigned char>(left[a])) != 0;
        const bool b_digit = std::isdigit(static_cast<unsigned char>(right[b])) != 0;
        if (a_digit && b_digit) {
            std::size_t a_end = a;
            std::size_t b_end = b;
            while (a_end < left.size() &&
                   std::isdigit(static_cast<unsigned char>(left[a_end])) != 0) {
                ++a_end;
            }
            while (b_end < right.size() &&
                   std::isdigit(static_cast<unsigned char>(right[b_end])) != 0) {
                ++b_end;
            }
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
            if (a_trimmed.size() != b_trimmed.size()) {
                return a_trimmed.size() < b_trimmed.size();
            }
            if (a_trimmed != b_trimmed) {
                return a_trimmed < b_trimmed;
            }
            a = a_end;
            b = b_end;
        } else {
            const char ac = static_cast<char>(std::tolower(static_cast<unsigned char>(left[a])));
            const char bc = static_cast<char>(std::tolower(static_cast<unsigned char>(right[b])));
            if (ac != bc) {
                return ac < bc;
            }
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
            const std::string prefix = directory.filename().string().empty()
                                           ? std::string{}
                                           : directory.filename().string() + "/";
            directory_cases.push_back(Case{
                prefix + entry.path().filename().string(),
                entry.path(),
                fs::is_regular_file(answer) ? std::optional<fs::path>(answer) : std::nullopt,
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

void show_stream_if_nonempty(const std::string& label, const fs::path& path, std::ostream& output) {
    if (fs::is_regular_file(path) && fs::file_size(path) != 0) {
        const std::string contents = read_file(path);
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

void atomic_pair(const fs::path& input_path, const fs::path& output_path, const std::string& input,
                 const std::string& output) {
    fs::create_directories(input_path.parent_path());
    const std::string suffix = ".tmp." + std::to_string(::getpid());
    const fs::path input_temp = input_path.string() + suffix;
    const fs::path output_temp = output_path.string() + suffix;
    const fs::path input_backup = input_path.string() + ".backup." + std::to_string(::getpid());
    const fs::path output_backup = output_path.string() + ".backup." + std::to_string(::getpid());
    bool input_backed_up = false;
    bool output_backed_up = false;
    bool input_installed = false;
    bool output_installed = false;
    write_file(input_temp, input);
    try {
        write_file(output_temp, output);
        if (fs::exists(input_path)) {
            fs::rename(input_path, input_backup);
            input_backed_up = true;
        }
        if (fs::exists(output_path)) {
            fs::rename(output_path, output_backup);
            output_backed_up = true;
        }
        fs::rename(input_temp, input_path);
        input_installed = true;
        fs::rename(output_temp, output_path);
        output_installed = true;
        std::error_code ignored;
        fs::remove(input_backup, ignored);
        fs::remove(output_backup, ignored);
    } catch (...) {
        std::error_code ignored;
        fs::remove(input_temp, ignored);
        fs::remove(output_temp, ignored);
        if (input_installed) {
            fs::remove(input_path, ignored);
        }
        if (output_installed) {
            fs::remove(output_path, ignored);
        }
        if (input_backed_up && fs::exists(input_backup)) {
            fs::rename(input_backup, input_path, ignored);
        }
        if (output_backed_up && fs::exists(output_backup)) {
            fs::rename(output_backup, output_path, ignored);
        }
        throw;
    }
}

} // namespace

Judge::Judge(fs::path root) : root_(fs::weakly_canonical(std::move(root))), builder_(root_) {}

TestSummary Judge::test(const Problem& problem, const TestOptions& options) const {
    TestSummary summary;
    summary.build =
        builder_.build_problem(problem, BuildOptions{options.checked, options.rebuild, true});
    if (!options.concise) {
        std::cout << (summary.build.compiled ? "built: " : "cached: ") << problem.id() << " ["
                  << summary.build.digest.substr(0, 12) << "]\n"
                  << "size: source " << format_bytes(summary.build.source_size) << ", binary "
                  << format_bytes(summary.build.binary_size) << '\n';
    }

    const std::vector<Case> cases = cases_for(problem);
    if (cases.empty()) {
        std::cout << "no tests found\n";
        return summary;
    }

    const fs::path run_directory = root_ / ".build" / "runs" / problem.id();
    fs::create_directories(run_directory);

    int number = 0;
    for (const Case& test_case : cases) {
        ++number;
        ++summary.total;
        const fs::path actual_path = run_directory / ("actual-" + std::to_string(number) + ".txt");
        const fs::path error_path = run_directory / ("error-" + std::to_string(number) + ".txt");

        if (!options.concise) {
            std::cout << "==> " << test_case.name << '\n';
        }
        if (!test_case.expected) {
            std::cout << (options.concise ? test_case.name + ": " : "")
                      << "missing expected output: " << test_case.input.stem().string() << ".out\n";
            continue;
        }
        const ProcessResult result =
            run_process({summary.build.binary.string()}, ProcessOptions{
                                                             test_case.input,
                                                             actual_path,
                                                             error_path,
                                                             options.timeout,
                                                             problem.solution_path().parent_path(),
                                                         });
        summary.elapsed += result.elapsed;
        if (result.status != 0) {
            std::cout << (options.concise ? test_case.name + ": " : "")
                      << (result.timed_out ? "TLE" : "runtime failure") << " (exit "
                      << result.status << ", " << format_duration(result.elapsed) << ")\n";
            show_stream_if_nonempty("stderr", error_path, std::cerr);
            continue;
        }

        show_stream_if_nonempty("stderr", error_path, std::cerr);
        const std::string actual = read_file(actual_path);
        const std::string expected = read_file(*test_case.expected);
        if (normalize_output(actual) == normalize_output(expected)) {
            if (!options.concise) {
                std::cout << "OK (" << format_duration(result.elapsed) << ")\n";
            }
            ++summary.passed;
        } else {
            std::cout << (options.concise ? test_case.name + ": " : "") << "WA ("
                      << format_duration(result.elapsed) << ")\n"
                      << "expected:\n"
                      << preview(expected)
                      << (expected.empty() || expected.back() == '\n' ? "" : "\n") << "actual:\n"
                      << preview(actual) << (actual.empty() || actual.back() == '\n' ? "" : "\n");
        }
    }
    if (options.concise) {
        std::cout << summary.passed << '/' << summary.total << " tests passed\n";
    } else {
        std::cout << summary.passed << '/' << summary.total << " passed, time "
                  << format_duration(summary.elapsed) << '\n';
    }
    return summary;
}

StressSummary Judge::stress(const Problem& problem, const StressOptions& options) const {
    if (options.iterations < 1) {
        throw std::runtime_error("stress iteration count must be positive");
    }
    const fs::path failure_directory = root_ / ".build" / "failures" / problem.id();
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
                                               std::nullopt,
                                               input,
                                               generator_error,
                                               options.generator_timeout,
                                               options.generator.parent_path(),
                                           });
        if (generated.status != 0) {
            throw std::runtime_error("generator failed for seed " + std::to_string(seed));
        }

        const ProcessResult expected_run =
            run_process({brute.binary.string()}, ProcessOptions{
                                                     input,
                                                     expected,
                                                     brute_error,
                                                     options.timeout,
                                                     options.brute.parent_path(),
                                                 });
        if (expected_run.status != 0) {
            throw std::runtime_error("brute force failed for seed " + std::to_string(seed));
        }
        const ProcessResult actual_run =
            run_process({solution.binary.string()}, ProcessOptions{
                                                        input,
                                                        actual,
                                                        solution_error,
                                                        options.timeout,
                                                        problem.solution_path().parent_path(),
                                                    });
        if (actual_run.status != 0 ||
            normalize_output(read_file(actual)) != normalize_output(read_file(expected))) {
            write_file(failure_directory / "seed.txt", std::to_string(seed) + "\n");
            write_file(ready, "failure\n");
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
    const fs::path failure_directory = root_ / ".build" / "failures" / problem.id();
    if (!fs::is_regular_file(failure_directory / "ready") ||
        !fs::is_regular_file(failure_directory / "input.txt") ||
        !fs::is_regular_file(failure_directory / "expected.txt")) {
        throw std::runtime_error("no current stress failure to promote for " + problem.id());
    }

    const fs::path directory =
        problem.uses_new_layout() ? problem.cases_path() : problem.legacy_tests_path();
    const std::string prefix = problem.uses_new_layout() ? "stress-" : "case-";
    int number = 1;
    fs::path input;
    fs::path output;
    do {
        input = directory / (prefix + std::to_string(number) + ".in");
        output = directory / (prefix + std::to_string(number) + ".out");
        ++number;
    } while (fs::exists(input) || fs::exists(output));

    atomic_pair(input, output, read_file(failure_directory / "input.txt"),
                read_file(failure_directory / "expected.txt"));
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

} // namespace cfprobs
