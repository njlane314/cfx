#include "browser.hpp"
#include "bundle.hpp"
#include "codeforces.hpp"
#include "companion.hpp"
#include "judge.hpp"
#include "problem.hpp"
#include "process.hpp"
#include "submission.hpp"
#include "workspace.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
using cfprobs::Problem;

const char* kHelp = R"(usage:
  probs PROBLEM
  probs submit

A small C++20 Codeforces workbench.

daily:
  probs 71A             fetch samples and open solution.cpp
  probs submit          test, checked-build, and submit through the browser

advanced:
  test [PROBLEM]        build and judge saved samples and cases
  stress [PROBLEM]      compare the solution with a brute force program
  bundle [PROBLEM]      write submission-ready source
  fail [PROBLEM]        promote the current stress failure
  cc                    receive samples from Competitive Companion
  get PROBLEM|CONTEST   create a workspace without opening an editor
  help [COMMAND]        show general or command help

Problems are canonically 71A. A.71, A 71 (as two arguments), Codeforces URLs, legacy
workspaces, and inference from problems/cf/<contest>/<index>/ are supported.
)";

const std::map<std::string, std::string, std::less<>> kCommandHelp{
    {"get", R"(usage: probs get PROBLEM|CONTEST

Create problems/cf/<contest>/<index>/ from templates/solution.cpp. A numeric
contest fetches its problem indexes from the official Codeforces API.
)"},
    {"test", R"(usage: probs test [--checked] [--rebuild] [--time-limit SECONDS] [PROBLEM]

Bundle, compile, and judge samples followed by handwritten cases. The problem
is inferred when the command runs inside its workspace.
)"},
    {"bundle", R"(usage: probs bundle [--output FILE] [PROBLEM]

Recursively expand project-local quoted includes. Output goes to stdout unless
--output is given.
)"},
    {"stress", R"(usage: probs stress [options] [PROBLEM]

Defaults to stress/gen.cpp and stress/brute.cpp in the problem workspace.
Options: --gen FILE, --brute FILE, -n/--count N, --seed N, --gen-arg ARG,
--checked, --rebuild, --time-limit SECONDS, --generator-time-limit SECONDS,
and --verbose.
)"},
    {"cc", R"(usage: probs cc [--host ADDRESS] [--port PORT] [--once] [--force]

Listen for Competitive Companion JSON and store fetched samples separately
from handwritten cases. Existing differing pairs require --force.
)"},
    {"submit", R"(usage: probs submit [--manual] [--rebuild] [PROBLEM]

Run saved tests, create and checked-compile the final bundle, then submit it
through the installed browser connector and the browser's authenticated
Codeforces session. If Chrome has no connector, fall back to copying the exact
tested source and opening the submission page. --manual selects that fallback
directly. With no PROBLEM, use the current workspace or the most recent problem
opened by `probs PROBLEM`; conflicting targets require an explicit ID. No
password or cookie is read or stored by probs.
)"},
    {"fail", R"(usage: probs fail [PROBLEM]

Promote the most recent recorded stress mismatch to a regression case.
)"},
};

class Arguments {
  public:
    explicit Arguments(std::vector<std::string> values) : values_(std::move(values)) {}

    [[nodiscard]] bool empty() const noexcept {
        return position_ >= values_.size();
    }

    std::string take() {
        if (empty()) {
            throw std::runtime_error("missing command argument");
        }
        return values_[position_++];
    }

  private:
    std::vector<std::string> values_;
    std::size_t position_ = 0;
};

long long integer(const std::string& value, const std::string& name) {
    std::size_t parsed = 0;
    const long long result = std::stoll(value, &parsed);
    if (parsed != value.size()) {
        throw std::runtime_error(name + " must be an integer");
    }
    return result;
}

std::chrono::milliseconds duration(const std::string& value, const std::string& name) {
    std::size_t parsed = 0;
    const double seconds = std::stod(value, &parsed);
    if (parsed != value.size() || !std::isfinite(seconds) || seconds <= 0.0 || seconds > 86400.0) {
        throw std::runtime_error(name + " must be a positive number no greater than 86400");
    }
    return std::chrono::milliseconds(static_cast<long long>(std::llround(seconds * 1000.0)));
}

int bounded_integer(const std::string& value, const std::string& name, int minimum, int maximum) {
    const long long parsed = integer(value, name);
    if (parsed < minimum || parsed > maximum) {
        throw std::runtime_error(name + " must be between " + std::to_string(minimum) + " and " +
                                 std::to_string(maximum));
    }
    return static_cast<int>(parsed);
}

bool all_digits(std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](char character) {
        return character >= '0' && character <= '9';
    });
}

Problem resolve_problem(const std::vector<std::string>& values, const fs::path& root) {
    if (values.empty()) {
        const std::optional<Problem> inferred = Problem::infer(fs::current_path(), root);
        if (!inferred) {
            throw std::runtime_error("cannot infer a problem here; pass an ID such as 71A");
        }
        return *inferred;
    }
    if (values.size() == 1) {
        return Problem::parse(values.front(), root);
    }
    if (values.size() == 2) {
        return Problem::parse(values[0], values[1], root);
    }
    throw std::runtime_error("expected at most one problem ID");
}

Problem resolve_submission_problem(const std::vector<std::string>& values,
                                   const fs::path& root) {
    if (!values.empty()) {
        return resolve_problem(values, root);
    }
    const std::optional<Problem> inferred = Problem::infer(fs::current_path(), root);
    const std::optional<Problem> remembered = cfprobs::current_problem(root);
    if (inferred && remembered && inferred->id() != remembered->id()) {
        throw std::runtime_error("submission target is ambiguous: current directory is " +
                                 inferred->id() + " but current problem is " + remembered->id() +
                                 "; pass an ID such as `probs submit " + remembered->id() + "`");
    }
    if (inferred) {
        return *inferred;
    }
    if (remembered) {
        if (!fs::is_regular_file(remembered->solution_path())) {
            throw std::runtime_error("current problem " + remembered->id() +
                                     " has no solution; run probs " + remembered->id() +
                                     " again");
        }
        return *remembered;
    }
    throw std::runtime_error(
        "cannot determine which problem to submit; run probs 71A or pass a problem ID");
}

fs::path resolve_path(const fs::path& value) {
    return value.is_absolute() ? value : fs::current_path() / value;
}

void show_command_help(const std::string& command) {
    std::string canonical = command;
    if (canonical == "new") {
        canonical = "get";
    } else if (canonical == "run" || canonical == "rerun") {
        canonical = "test";
    }
    const auto help = kCommandHelp.find(canonical);
    if (help == kCommandHelp.end()) {
        throw std::runtime_error("unknown command: " + command);
    }
    std::cout << help->second;
}

std::string display_path(const fs::path& path, const fs::path& root) {
    std::error_code error;
    const fs::path relative = fs::relative(path, root, error);
    return error || relative.empty() ? path.string() : relative.string();
}

void open_editor(const fs::path& solution) {
    std::vector<std::string> command;
    if (const char* editor = std::getenv("EDITOR"); editor != nullptr && *editor != '\0') {
        command = cfprobs::split_command_words(editor);
    } else if (const char* visual = std::getenv("VISUAL"); visual != nullptr && *visual != '\0') {
        command = cfprobs::split_command_words(visual);
    } else {
#ifdef __APPLE__
        command = {"open"};
#else
        command = {"xdg-open"};
#endif
    }
    if (command.empty()) {
        throw std::runtime_error("EDITOR names no command");
    }
    command.push_back(solution.filename().string());
    const cfprobs::ProcessResult result = cfprobs::run_process(command, cfprobs::ProcessOptions{
                                                                            std::nullopt,
                                                                            std::nullopt,
                                                                            std::nullopt,
                                                                            std::nullopt,
                                                                            solution.parent_path(),
                                                                        });
    if (result.status != 0) {
        throw std::runtime_error("editor exited with status " + std::to_string(result.status));
    }
}

int command_get(Arguments arguments, const fs::path& root) {
    std::vector<std::string> positional;
    while (!arguments.empty()) {
        const std::string argument = arguments.take();
        if (argument == "--help" || argument == "-h") {
            show_command_help("get");
            return 0;
        }
        if (argument.starts_with("-")) {
            throw std::runtime_error("get: unknown option " + argument);
        }
        positional.push_back(argument);
    }
    if (positional.empty() || positional.size() > 2) {
        throw std::runtime_error("usage: probs get PROBLEM|CONTEST");
    }

    cfprobs::Workspace workspace(root);
    if (positional.size() == 1 && all_digits(positional.front())) {
        const std::string contest = positional.front();
        const std::vector<std::string> indexes = cfprobs::fetch_contest_indexes(contest);
        for (const std::string& index : indexes) {
            const cfprobs::WorkspaceResult result = workspace.create(Problem(contest, index, root));
            std::cout << (result.solution_created ? "created: " : "exists: ") << result.solution
                      << '\n';
        }
        return 0;
    }

    const Problem problem = resolve_problem(positional, root);
    const cfprobs::WorkspaceResult result = workspace.create(problem);
    std::cout << (result.solution_created ? "created: " : "exists: ") << result.solution << '\n';
    return 0;
}

int command_problem(const std::vector<std::string>& values, const fs::path& root) {
    const Problem problem = resolve_problem(values, root);
    const cfprobs::WorkspaceResult workspace = cfprobs::Workspace(root).create(problem);

    std::string payload;
    try {
        payload = cfprobs::fetch_problem_in_browser(cfprobs::problem_url(problem));
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string(error.what()) + "; use probs cc --once as a fallback");
    }
    const cfprobs::CompanionPackage package = cfprobs::parse_companion_package(payload, root);
    if (package.problem.id() != problem.id()) {
        throw std::runtime_error("browser returned " + package.problem.id() + " while fetching " +
                                 problem.id());
    }
    const cfprobs::ImportResult imported = cfprobs::import_companion_package(package, root, true);
    cfprobs::remember_current_problem(problem, root);

    std::cout << "Fetched " << problem.id();
    if (!package.name.empty()) {
        std::cout << " — " << package.name;
    }
    std::cout << '\n'
              << "Imported " << imported.sample_count << ' '
              << (imported.sample_count == 1 ? "sample" : "samples") << '\n'
              << "Opened " << display_path(workspace.solution, root) << '\n';
    open_editor(workspace.solution);
    return 0;
}

int command_test(Arguments arguments, const fs::path& root) {
    cfprobs::TestOptions options;
    std::vector<std::string> positional;
    while (!arguments.empty()) {
        const std::string argument = arguments.take();
        if (argument == "--help" || argument == "-h") {
            show_command_help("test");
            return 0;
        }
        if (argument == "--checked" || argument == "--check") {
            options.checked = true;
        } else if (argument == "--rebuild") {
            options.rebuild = true;
        } else if (argument == "--time-limit") {
            options.timeout = duration(arguments.take(), "--time-limit");
        } else if (argument.starts_with("-")) {
            throw std::runtime_error("test: unknown option " + argument);
        } else {
            positional.push_back(argument);
        }
    }
    const Problem problem = resolve_problem(positional, root);
    const cfprobs::TestSummary result = cfprobs::Judge(root).test(problem, options);
    return result.success() ? 0 : 1;
}

int command_bundle(Arguments arguments, const fs::path& root) {
    std::optional<fs::path> output;
    std::vector<std::string> positional;
    while (!arguments.empty()) {
        const std::string argument = arguments.take();
        if (argument == "--help" || argument == "-h") {
            show_command_help("bundle");
            return 0;
        }
        if (argument == "--output" || argument == "-o") {
            output = resolve_path(arguments.take());
        } else if (argument.starts_with("-")) {
            throw std::runtime_error("bundle: unknown option " + argument);
        } else {
            positional.push_back(argument);
        }
    }
    const Problem problem = resolve_problem(positional, root);
    const std::string source = cfprobs::bundle(problem.solution_path(), root);
    if (!output) {
        std::cout << source;
    } else {
        fs::create_directories(output->parent_path());
        std::ofstream stream(*output, std::ios::binary | std::ios::trunc);
        if (!stream || !(stream << source)) {
            throw std::runtime_error("cannot write " + output->string());
        }
        std::cout << *output << '\n';
    }
    return 0;
}

int command_stress(Arguments arguments, const fs::path& root) {
    cfprobs::StressOptions options;
    std::optional<fs::path> generator;
    std::optional<fs::path> brute;
    std::vector<std::string> positional;
    while (!arguments.empty()) {
        const std::string argument = arguments.take();
        if (argument == "--help" || argument == "-h") {
            show_command_help("stress");
            return 0;
        }
        if (argument == "--gen") {
            generator = resolve_path(arguments.take());
        } else if (argument == "--brute") {
            brute = resolve_path(arguments.take());
        } else if (argument == "-n" || argument == "--count") {
            options.iterations = bounded_integer(arguments.take(), "--count", 1, 1000000);
        } else if (argument == "--seed") {
            const long long value = integer(arguments.take(), "--seed");
            if (value < 0) {
                throw std::runtime_error("--seed must be non-negative");
            }
            options.seed = static_cast<std::uint64_t>(value);
        } else if (argument == "--gen-arg") {
            options.generator_arguments.push_back(arguments.take());
        } else if (argument == "--checked" || argument == "--check") {
            options.checked = true;
        } else if (argument == "--rebuild") {
            options.rebuild = true;
        } else if (argument == "--verbose") {
            options.verbose = true;
        } else if (argument == "--time-limit") {
            options.timeout = duration(arguments.take(), "--time-limit");
        } else if (argument == "--generator-time-limit") {
            options.generator_timeout = duration(arguments.take(), "--generator-time-limit");
        } else if (argument.starts_with("-")) {
            throw std::runtime_error("stress: unknown option " + argument);
        } else {
            positional.push_back(argument);
        }
    }
    const Problem problem = resolve_problem(positional, root);
    if (generator) {
        options.generator = *generator;
    } else {
        const fs::path preferred = problem.stress_path() / "gen.cpp";
        const fs::path legacy = root / "stress" / (problem.legacy_id() + ".gen.cpp");
        options.generator =
            !fs::is_regular_file(preferred) && fs::is_regular_file(legacy) ? legacy : preferred;
    }
    if (brute) {
        options.brute = *brute;
    } else {
        const fs::path preferred = problem.stress_path() / "brute.cpp";
        const fs::path legacy = root / "stress" / (problem.legacy_id() + ".brute.cpp");
        options.brute =
            !fs::is_regular_file(preferred) && fs::is_regular_file(legacy) ? legacy : preferred;
    }
    const cfprobs::StressSummary result = cfprobs::Judge(root).stress(problem, options);
    return result.success() ? 0 : 1;
}

int command_fail(Arguments arguments, const fs::path& root) {
    std::vector<std::string> positional;
    while (!arguments.empty()) {
        const std::string argument = arguments.take();
        if (argument == "--help" || argument == "-h") {
            show_command_help("fail");
            return 0;
        }
        if (argument.starts_with("-")) {
            throw std::runtime_error("fail: unknown option " + argument);
        }
        positional.push_back(argument);
    }
    const Problem problem = resolve_problem(positional, root);
    const auto [input, output] = cfprobs::Judge(root).promote_failure(problem);
    std::cout << "wrote: " << input << '\n' << "wrote: " << output << '\n';
    return 0;
}

int command_cc(Arguments arguments, const fs::path& root) {
    std::string host = "127.0.0.1";
    int port = 27121;
    bool once = false;
    bool force = false;
    while (!arguments.empty()) {
        const std::string argument = arguments.take();
        if (argument == "--help" || argument == "-h") {
            show_command_help("cc");
            return 0;
        }
        if (argument == "--host") {
            host = arguments.take();
        } else if (argument == "--port") {
            port = bounded_integer(arguments.take(), "--port", 1, 65535);
        } else if (argument == "--once") {
            once = true;
        } else if (argument == "--force") {
            force = true;
        } else {
            throw std::runtime_error("cc: unknown option " + argument);
        }
    }
    cfprobs::serve_companion(root, host, port, once, force);
    return 0;
}

int command_submit(Arguments arguments, const fs::path& root) {
    bool rebuild = false;
    bool manual = false;
    std::vector<std::string> positional;
    while (!arguments.empty()) {
        const std::string argument = arguments.take();
        if (argument == "--help" || argument == "-h") {
            show_command_help("submit");
            return 0;
        }
        if (argument == "--rebuild") {
            rebuild = true;
        } else if (argument == "--manual") {
            manual = true;
        } else if (argument.starts_with("-")) {
            throw std::runtime_error("submit: unknown option " + argument);
        } else {
            positional.push_back(argument);
        }
    }

    const Problem problem = resolve_submission_problem(positional, root);
    const cfprobs::SubmissionArtifact artifact =
        cfprobs::prepare_submission(root, problem, rebuild);
    std::cout << "Checked build passed\n";

    const auto prepare_manual = [&] {
        cfprobs::copy_submission_to_clipboard(artifact);
        std::cout << "Copied tested bundle " << artifact.source_hash.substr(0, 16)
                  << " to the clipboard\n";
        cfprobs::open_browser_url(artifact.page_url);
        std::cout << "Opened Codeforces submission page for " << artifact.target << '\n'
                  << "Paste and submit as " << artifact.language << '\n';
    };
    if (manual) {
        prepare_manual();
        return 0;
    }

    cfprobs::BrowserSubmitReceipt receipt;
    try {
        receipt = cfprobs::submit_in_browser(cfprobs::BrowserSubmitRequest{
            artifact.page_url,
            artifact.target,
            problem.index(),
            artifact.language,
            artifact.source_text,
        });
    } catch (const cfprobs::BrowserConnectorUnavailable&) {
        std::cout << "Chrome connector unavailable; using manual submission\n";
        prepare_manual();
        return 0;
    }
    std::cout << "Submitted " << artifact.target << " as " << artifact.language << '\n'
              << receipt.submission_url << '\n';
    if (!receipt.verdict.empty() && receipt.verdict != "submitted") {
        std::cout << "Verdict: " << receipt.verdict << '\n';
    }
    return 0;
}

fs::path select_root(std::vector<std::string>& values) {
    std::optional<fs::path> explicit_root;
    for (auto iterator = values.begin(); iterator != values.end();) {
        if (*iterator == "--root") {
            const auto value = std::next(iterator);
            if (value == values.end()) {
                throw std::runtime_error("--root needs a path");
            }
            explicit_root = *value;
            iterator = values.erase(iterator, std::next(value));
        } else {
            ++iterator;
        }
    }
    if (explicit_root) {
        return fs::weakly_canonical(*explicit_root);
    }
    const char* environment_root = std::getenv("CF_PROBS_ROOT");
    if (environment_root != nullptr && *environment_root != '\0') {
        return fs::weakly_canonical(environment_root);
    }
    return cfprobs::find_workspace_root(fs::current_path());
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::vector<std::string> values;
        for (int index = 1; index < argc; ++index) {
            values.emplace_back(argv[index]);
        }
        const fs::path root = select_root(values);
        Arguments arguments(std::move(values));
        if (arguments.empty()) {
            std::cout << kHelp;
            return 0;
        }

        std::string command = arguments.take();
        if (command == "--help" || command == "-h") {
            std::cout << kHelp;
            return 0;
        }
        if (command == "help") {
            if (arguments.empty()) {
                std::cout << kHelp;
            } else {
                show_command_help(arguments.take());
                if (!arguments.empty()) {
                    throw std::runtime_error("help accepts one command name");
                }
            }
            return 0;
        }

        if (command == "new") {
            command = "get";
        } else if (command == "run" || command == "rerun") {
            command = "test";
        }
        if (command == "get") {
            return command_get(std::move(arguments), root);
        }
        if (command == "test") {
            return command_test(std::move(arguments), root);
        }
        if (command == "bundle") {
            return command_bundle(std::move(arguments), root);
        }
        if (command == "stress") {
            return command_stress(std::move(arguments), root);
        }
        if (command == "fail") {
            return command_fail(std::move(arguments), root);
        }
        if (command == "cc") {
            return command_cc(std::move(arguments), root);
        }
        if (command == "submit") {
            return command_submit(std::move(arguments), root);
        }
        std::vector<std::string> problem{std::move(command)};
        while (!arguments.empty()) {
            problem.push_back(arguments.take());
        }
        return command_problem(problem, root);
    } catch (const std::exception& error) {
        std::cerr << "probs: " << error.what() << '\n';
        return 2;
    }
}
