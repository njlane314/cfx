#include "commands.hpp"

#include "browser.hpp"
#include "codeforces.hpp"
#include "companion.hpp"
#include "judge.hpp"
#include "problem.hpp"
#include "process.hpp"
#include "submission.hpp"
#include "workspace.hpp"

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
#include <vector>

namespace cfx::cli {

namespace fs = std::filesystem;
using cfx::Problem;

const char* kHelp = R"(usage:
  cfx PROBLEM
  cfx submit

A small C++20 Codeforces workbench.

daily:
  cfx 71A             fetch samples and open solution.cpp
  cfx submit          test, checked-build, and submit through the browser

advanced:
  test [PROBLEM]        build and judge saved samples and cases
  help [COMMAND]        show general or command help

Problems use canonical IDs such as 71A. Commands also infer the problem from
codeforces/<contest>/<index>/.
)";

const std::map<std::string, std::string, std::less<>> kCommandHelp{
    {"test", R"(usage: cfx test [options] [PROBLEM]

Bundle, compile, and judge samples followed by handwritten cases. The problem
is inferred when the command runs inside its archive directory. Fetched
Codeforces time and memory limits are used by default. Options: --checked,
--rebuild, --time-limit SECONDS, --memory-limit MIB, and --output-limit MIB.
)"},
    {"submit", R"(usage: cfx submit [--manual] [--rebuild] [PROBLEM]

Run saved tests, create and checked-compile the final bundle, then submit it
through the installed browser connector and the browser's authenticated
Codeforces session. If Chrome has no connector, fall back to copying the exact
tested source and opening the submission page. --manual selects that fallback
directly. With no PROBLEM, use the current directory or the most recent problem opened by
`cfx PROBLEM`; conflicting targets require an explicit ID. No password or
cookie is read or stored by cfx.
Exit status is 0 only for `OK` on `TESTS`, 1 for a final non-Accepted verdict,
and 2 while judging is pending or submission requires manual completion.
)"},
};

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

int command_problem(const std::vector<std::string>& values, const fs::path& root) {
    const Problem problem = resolve_problem(values, root);
    const fs::path solution = cfx::Workspace(root).create(problem);

    const std::string payload = cfx::fetch_problem_in_browser(cfx::problem_url(problem));
    const cfx::CompanionPackage package = cfx::parse_companion_package(payload, root);
    if (package.problem.id() != problem.id()) {
        throw std::runtime_error("browser returned " + package.problem.id() + " while fetching " +
                                 problem.id());
    }
    cfx::import_companion_package(package, root);
    cfx::remember_current_problem(problem, root);

    std::cout << "Fetched " << problem.id();
    if (!package.name.empty()) {
        std::cout << " — " << package.name;
    }
    std::cout << '\n'
              << "Imported " << package.samples.size() << ' '
              << (package.samples.size() == 1 ? "sample" : "samples") << '\n'
              << "Opened " << display_path(solution, root) << '\n';
    open_editor(solution);
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
        if (argument == "--checked") {
            options.checked = true;
        } else if (argument == "--rebuild") {
            options.rebuild = true;
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

} // namespace cfx::cli
