#include "commands.hpp"

#include "browser.hpp"
#include "bundle.hpp"
#include "codeforces.hpp"
#include "companion.hpp"
#include "compiler.hpp"
#include "file.hpp"
#include "judge.hpp"
#include "problem.hpp"
#include "process.hpp"
#include "recorder.hpp"
#include "submission.hpp"
#include "workspace.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace cfx::cli {

namespace fs = std::filesystem;
using cfx::Problem;

const char* kHelp = R"(usage:
  cfx PROBLEM
  cfx submit
  cfx sync [CONTEST|PROBLEM]

A small C++20 Codeforces workbench.

daily:
  cfx 71A             fetch samples and open solution.cpp
  cfx submit          test, checked-build, and submit through the browser
  cfx sync            reconcile final judging

advanced:
  test [PROBLEM]        build and judge saved samples and cases
  stress [PROBLEM]      compare the solution with a brute force program
  bundle [PROBLEM]      write submission-ready source
  fail [PROBLEM]        promote the current stress failure
  cc                    receive samples from Competitive Companion
  get PROBLEM|CONTEST   create problem files without opening an editor
  help [COMMAND]        show general or command help

Problems are canonically 71A. A.71, A 71 (as two arguments), Codeforces URLs,
and inference from codeforces/<contest>/<index>/ are supported.
Set `git config cfx.record commit` in the solutions repo to record final
acceptances locally. cfx never pushes.
)";

const std::map<std::string, std::string, std::less<>> kCommandHelp{
    {"get", R"(usage: cfx get PROBLEM|CONTEST

Create codeforces/<contest>/<index>/ from the archive or packaged solution
template. A numeric contest fetches indexes from the official Codeforces API.
)"},
    {"test", R"(usage: cfx test [options] [PROBLEM]

Bundle, compile, and judge samples followed by handwritten cases. The problem
is inferred when the command runs inside its archive directory. Fetched
Codeforces time and memory limits are used by default. --remote-check runs
inputs under their limits without comparing saved answers; output is reported
unchecked. Options: --checked, --rebuild, --remote-check, --time-limit SECONDS,
--memory-limit MIB, and --output-limit MIB.
)"},
    {"bundle", R"(usage: cfx bundle [--output FILE] [PROBLEM]

Recursively expand project-local quoted includes. Output goes to stdout unless
--output is given.
)"},
    {"stress", R"(usage: cfx stress [options] [PROBLEM]

Defaults to stress/gen.cpp and stress/brute.cpp in the problem directory.
Options: --gen FILE, --brute FILE, -n/--count N, --seed N, --gen-arg ARG,
--checked, --rebuild, --time-limit SECONDS, --generator-time-limit SECONDS,
and --verbose.
)"},
    {"cc", R"(usage: cfx cc [--host ADDRESS] [--port PORT] [--once] [--force]

Listen for Competitive Companion JSON and store fetched samples separately
from handwritten cases. Existing differing pairs require --force.
)"},
    {"submit", R"(usage: cfx submit [--manual] [--rebuild] [--remote-check] [PROBLEM]

Run saved tests, create and checked-compile the final bundle, then submit it
through the installed browser connector and the browser's authenticated
Codeforces session. --remote-check skips saved-answer comparison only; build or
execution failures still stop submission, and Codeforces decides correctness.
If Chrome has no connector, fall back to copying the exact tested source and
opening the submission page. --manual selects that fallback directly. With no
PROBLEM, use the current directory or the most recent problem opened by
`cfx PROBLEM`; conflicting targets require an explicit ID. No password or
cookie is read or stored by cfx.
Exit status is 0 only for `OK` on `TESTS`, 1 for a final non-Accepted verdict,
and 2 while judging is pending or submission requires manual completion.
)"},
    {"sync", R"(usage: cfx sync [CONTEST|PROBLEM]

Recheck saved submissions after final judging and record confirmed acceptances.
Set `git config cfx.record commit` to create local acceptance commits. Repeated
runs are safe. Never pushes.
)"},
    {"fail", R"(usage: cfx fail [PROBLEM]

Promote the most recent recorded stress mismatch to a regression case.
)"},
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
    const long long milliseconds = static_cast<long long>(std::llround(seconds * 1000.0));
    if (milliseconds < 1) {
        throw std::runtime_error(name + " must be at least 0.001 seconds");
    }
    return std::chrono::milliseconds(milliseconds);
}

std::uint64_t mebibytes(const std::string& value, const std::string& name) {
    std::size_t parsed = 0;
    const double count = std::stod(value, &parsed);
    if (parsed != value.size() || !std::isfinite(count) || count <= 0.0 || count > 1'048'576.0) {
        throw std::runtime_error(name + " must be a positive number no greater than 1048576");
    }
    const double bytes = count * 1024.0 * 1024.0;
    const auto rounded = static_cast<std::uint64_t>(std::llround(bytes));
    if (rounded == 0) {
        throw std::runtime_error(name + " is too small");
    }
    return rounded;
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
    const std::optional<Problem> remembered = cfx::current_problem(root);
    if (inferred && remembered && inferred->id() != remembered->id()) {
        throw std::runtime_error("submission target is ambiguous: current directory is " +
                                 inferred->id() + " but current problem is " + remembered->id() +
                                 "; pass an ID such as `cfx submit " + remembered->id() + "`");
    }
    if (inferred) {
        return *inferred;
    }
    if (remembered) {
        if (!fs::is_regular_file(remembered->solution_path())) {
            throw std::runtime_error("current problem " + remembered->id() +
                                     " has no solution; run cfx " + remembered->id() +
                                     " again");
        }
        return *remembered;
    }
    throw std::runtime_error(
        "cannot determine which problem to submit; run cfx 71A or pass a problem ID");
}

fs::path resolve_path(const fs::path& value) {
    return value.is_absolute() ? value : fs::current_path() / value;
}

void show_help() {
    std::cout << kHelp;
}

void show_command_help(const std::string& command) {
    const auto help = kCommandHelp.find(command);
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
        command = cfx::split_command_words(editor);
    } else if (const char* visual = std::getenv("VISUAL"); visual != nullptr && *visual != '\0') {
        command = cfx::split_command_words(visual);
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
    const cfx::ProcessResult result = cfx::run_process(command, cfx::ProcessOptions{
                                                                            .working_directory =
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
        throw std::runtime_error("usage: cfx get PROBLEM|CONTEST");
    }

    cfx::Workspace workspace(root);
    if (positional.size() == 1 && all_digits(positional.front())) {
        const std::string contest = positional.front();
        const std::vector<std::string> indexes = cfx::fetch_contest_indexes(contest);
        for (const std::string& index : indexes) {
            const cfx::WorkspaceResult result = workspace.create(Problem(contest, index, root));
            std::cout << (result.solution_created ? "created: " : "exists: ") << result.solution
                      << '\n';
        }
        return 0;
    }

    const Problem problem = resolve_problem(positional, root);
    const cfx::WorkspaceResult result = workspace.create(problem);
    std::cout << (result.solution_created ? "created: " : "exists: ") << result.solution << '\n';
    return 0;
}

int command_problem(const std::vector<std::string>& values, const fs::path& root) {
    const Problem problem = resolve_problem(values, root);
    const cfx::WorkspaceResult workspace = cfx::Workspace(root).create(problem);

    std::string payload;
    try {
        payload = cfx::fetch_problem_in_browser(cfx::problem_url(problem));
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string(error.what()) + "; use cfx cc --once as a fallback");
    }
    const cfx::CompanionPackage package = cfx::parse_companion_package(payload, root);
    if (package.problem.id() != problem.id()) {
        throw std::runtime_error("browser returned " + package.problem.id() + " while fetching " +
                                 problem.id());
    }
    const cfx::ImportResult imported = cfx::import_companion_package(package, root, true);
    cfx::remember_current_problem(problem, root);

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
    cfx::TestOptions options;
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
        } else if (argument == "--remote-check") {
            options.remote_check = true;
        } else if (argument == "--time-limit") {
            options.timeout = duration(arguments.take(), "--time-limit");
        } else if (argument == "--memory-limit") {
            options.memory_limit_bytes = mebibytes(arguments.take(), "--memory-limit");
        } else if (argument == "--output-limit") {
            options.output_limit_bytes = mebibytes(arguments.take(), "--output-limit");
        } else if (argument.starts_with("-")) {
            throw std::runtime_error("test: unknown option " + argument);
        } else {
            positional.push_back(argument);
        }
    }
    const Problem problem = resolve_problem(positional, root);
    const cfx::TestSummary result = cfx::Judge(root).test(problem, options);
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
    const std::string source = cfx::bundle(problem.solution_path(), root);
    if (!output) {
        std::cout << source;
    } else {
        fs::create_directories(output->parent_path());
        cfx::write_text(*output, source);
        std::cout << *output << '\n';
    }
    return 0;
}

int command_stress(Arguments arguments, const fs::path& root) {
    cfx::StressOptions options;
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
        options.generator = problem.stress_path() / "gen.cpp";
    }
    if (brute) {
        options.brute = *brute;
    } else {
        options.brute = problem.stress_path() / "brute.cpp";
    }
    const cfx::StressSummary result = cfx::Judge(root).stress(problem, options);
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
    const auto [input, output] = cfx::Judge(root).promote_failure(problem);
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
    cfx::serve_companion(root, host, port, once, force);
    return 0;
}

int command_submit(Arguments arguments, const fs::path& root) {
    cfx::SubmissionOptions options;
    bool manual = false;
    std::vector<std::string> positional;
    while (!arguments.empty()) {
        const std::string argument = arguments.take();
        if (argument == "--help" || argument == "-h") {
            show_command_help("submit");
            return 0;
        }
        if (argument == "--rebuild") {
            options.rebuild = true;
        } else if (argument == "--remote-check") {
            options.remote_check = true;
        } else if (argument == "--manual") {
            manual = true;
        } else if (argument.starts_with("-")) {
            throw std::runtime_error("submit: unknown option " + argument);
        } else {
            positional.push_back(argument);
        }
    }

    const Problem problem = resolve_submission_problem(positional, root);
    const cfx::SubmissionArtifact artifact = cfx::prepare_submission(root, problem, options);
    std::cout << "Checked build passed\n";

    const auto prepare_manual = [&] {
        cfx::copy_submission_to_clipboard(artifact);
        std::cout << "Copied tested bundle " << artifact.source_hash.substr(0, 16)
                  << " to the clipboard\n";
        cfx::open_browser_url(artifact.page_url);
        std::cout << "Opened Codeforces submission page for " << artifact.target << '\n'
                  << "Paste and submit as " << artifact.language << '\n';
    };
    if (manual) {
        prepare_manual();
        return 2;
    }

    cfx::BrowserSubmitReceipt receipt;
    try {
        receipt = cfx::submit_in_browser(cfx::BrowserSubmitRequest{
            artifact.page_url,
            artifact.target,
            problem.index(),
            artifact.language,
            artifact.source_text,
        });
    } catch (const cfx::BrowserConnectorUnavailable&) {
        std::cout << "Chrome connector unavailable; using manual submission\n";
        prepare_manual();
        return 2;
    }
    std::cout << "Submitted " << artifact.target << " as " << artifact.language << '\n'
              << "Submission: " << receipt.submission_id << '\n'
              << "URL: " << receipt.submission_url << '\n';
    const bool accepted = receipt.verdict == "OK" && receipt.testset == "TESTS";
    const bool pretests = receipt.verdict == "OK" && receipt.testset == "PRETESTS";
    if (!receipt.participant_type.empty()) {
        std::cout << "Participation: " << receipt.participant_type << '\n';
    }
    if (receipt.verdict.empty()) {
        std::cout << "Verdict: Pending\n";
    } else {
        std::cout << "Verdict: ";
        if (pretests) {
            std::cout << "Pretests passed";
        } else if (receipt.verdict == "OK" && !accepted) {
            std::cout << "Pending";
            if (!receipt.testset.empty()) std::cout << " (" << receipt.testset << ')';
        } else {
            std::cout << receipt.verdict_text;
        }
        std::cout << '\n'
                  << "Tests passed: " << receipt.passed_test_count << '\n'
                  << "Time: " << receipt.time_consumed_millis << " ms\n"
                  << "Memory: " << cfx::format_bytes(receipt.memory_consumed_bytes) << '\n';
    }
    std::cout << "Judging wait: "
              << cfx::format_duration(std::chrono::milliseconds(receipt.judging_wait_millis))
              << '\n';

    std::optional<cfx::RecordedSubmission> recorded;
    try {
        recorded = cfx::record_submission(root, problem, artifact, receipt);
    } catch (const std::exception& error) {
        std::cerr << "warning: submission recording failed: " << error.what() << '\n';
    }
    if (recorded && recorded->commit) {
        std::cout << "Recorded: " << problem.id() << " ["
                  << recorded->commit->substr(0, 12) << "]\n";
    } else if (recorded && recorded->state == cfx::SubmissionState::accepted) {
        std::cout << "Receipt: " << recorded->receipt << '\n';
    }

    if (receipt.verdict.empty()) {
        std::cout << "Pending: "
                  << (receipt.pending_reason.empty() ? "Codeforces verdict" : receipt.pending_reason)
                  << '\n';
        return 2;
    }
    if (receipt.verdict == "OK" && !accepted) {
        std::cout << "Pending: final judging\n";
        return 2;
    }
    return accepted ? 0 : 1;
}

int command_sync(Arguments arguments, const fs::path& root) {
    std::vector<std::string> positional;
    while (!arguments.empty()) {
        const std::string argument = arguments.take();
        if (argument == "--help" || argument == "-h") {
            show_command_help("sync");
            return 0;
        }
        if (argument.starts_with("-")) {
            throw std::runtime_error("sync: unknown option " + argument);
        }
        positional.push_back(argument);
    }
    if (positional.size() > 2) {
        throw std::runtime_error("usage: cfx sync [CONTEST|PROBLEM]");
    }

    std::optional<std::string> contest_filter;
    std::optional<std::string> problem_filter;
    if (positional.size() == 1 && all_digits(positional.front())) {
        contest_filter = positional.front();
    } else if (!positional.empty()) {
        problem_filter = resolve_problem(positional, root).id();
    }

    std::vector<cfx::StoredSubmission> stored = cfx::unresolved_submissions(root);
    stored.erase(std::remove_if(stored.begin(), stored.end(), [&](const auto& submission) {
        const Problem problem = Problem::parse(submission.problem_id, root);
        return (contest_filter && problem.contest_id() != *contest_filter) ||
               (problem_filter && problem.id() != *problem_filter);
    }), stored.end());
    if (stored.empty()) {
        std::cout << "sync: up to date\n";
        return 0;
    }

    bool pending = false;
    bool rejected = false;
    std::optional<std::chrono::steady_clock::time_point> last_request;
    for (const cfx::StoredSubmission& submission : stored) {
        const Problem problem = Problem::parse(submission.problem_id, root);
        if (submission.state == cfx::SubmissionState::accepted) {
            try {
                const cfx::RecordedSubmission result =
                    cfx::retry_submission_recording(root, submission);
                std::cout << problem.id() << ": Accepted " << submission.submission_id;
                if (result.commit) {
                    std::cout << " [" << result.commit->substr(0, 12) << ']';
                }
                std::cout << '\n';
            } catch (const std::exception& error) {
                std::cout << problem.id() << ": not recorded (" << error.what() << ")\n";
                pending = true;
            }
            continue;
        }
        if (last_request) {
            std::this_thread::sleep_until(*last_request + std::chrono::milliseconds(2100));
        }

        cfx::CodeforcesSubmission status;
        try {
            status = cfx::fetch_submission_status(problem.contest_id(), problem.index(),
                                                  submission.handle,
                                                  submission.submission_id, 20);
        } catch (const std::exception& error) {
            last_request = std::chrono::steady_clock::now();
            std::string message = error.what();
            std::replace_if(message.begin(), message.end(), [](unsigned char character) {
                return std::isspace(character) != 0;
            }, ' ');
            std::cout << problem.id() << ": pending (" << message << ")\n";
            pending = true;
            continue;
        }
        last_request = std::chrono::steady_clock::now();
        if (!status.found || !status.terminal) {
            std::cout << problem.id() << ": pending\n";
            pending = true;
            continue;
        }

        try {
            const cfx::RecordedSubmission result =
                cfx::update_submission(root, submission, status);
            if (result.state == cfx::SubmissionState::pretests) {
                std::cout << problem.id() << ": pretests " << submission.submission_id;
                if (!status.participant_type.empty()) {
                    std::cout << " (" << status.participant_type << ')';
                }
                std::cout << '\n';
                pending = true;
            } else if (result.state == cfx::SubmissionState::accepted) {
                std::cout << problem.id() << ": Accepted " << submission.submission_id;
                if (result.commit) {
                    std::cout << " [" << result.commit->substr(0, 12) << ']';
                }
                std::cout << '\n';
            } else if (result.state == cfx::SubmissionState::rejected) {
                std::cout << problem.id() << ": " << status.verdict_text << ' '
                          << submission.submission_id << '\n';
                rejected = true;
            } else {
                std::cout << problem.id() << ": pending\n";
                pending = true;
            }
        } catch (const std::exception& error) {
            std::cout << problem.id() << ": not recorded (" << error.what() << ")\n";
            pending = true;
        }
    }
    if (pending) return 2;
    return rejected ? 1 : 0;
}

} // namespace cfx::cli
