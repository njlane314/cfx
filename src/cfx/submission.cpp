#include "submission.hpp"

#include "codeforces.hpp"
#include "compiler.hpp"
#include "file.hpp"
#include "hash.hpp"
#include "judge.hpp"
#include "problem.hpp"
#include "process.hpp"
#include "runtime.hpp"

#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace cfx {
namespace {

namespace fs = std::filesystem;

std::string language_name(std::string standard) {
    if (standard == "gnu++20" || standard == "c++20") {
        return "GNU C++20";
    }
    if (standard == "gnu++17" || standard == "c++17") {
        return "GNU C++17";
    }
    if (standard == "gnu++23" || standard == "c++23") {
        return "GNU C++23";
    }
    return standard;
}

#ifndef __APPLE__
bool environment_set(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0';
}

bool executable(std::string_view name) {
    if (name.empty()) {
        return false;
    }
    if (name.find('/') != std::string_view::npos) {
        return ::access(std::string(name).c_str(), X_OK) == 0;
    }
    const char* path_environment = std::getenv("PATH");
    const std::string_view path =
        path_environment == nullptr ? std::string_view() : path_environment;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t separator = path.find(':', start);
        const std::string_view directory = path.substr(
            start, separator == std::string_view::npos ? path.size() - start : separator - start);
        const fs::path candidate =
            (directory.empty() ? fs::current_path() : fs::path(directory)) / name;
        if (::access(candidate.c_str(), X_OK) == 0) {
            return true;
        }
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
    }
    return false;
}
#endif

std::vector<std::string> clipboard_command() {
    if (const char* configured = std::getenv("CFX_CLIPBOARD");
        configured != nullptr && *configured != '\0') {
        std::vector<std::string> command = split_command_words(configured);
        if (command.empty()) {
            throw std::runtime_error("CFX_CLIPBOARD is empty");
        }
        return command;
    }
#ifdef __APPLE__
    return {"pbcopy"};
#else
    const bool wayland = environment_set("WAYLAND_DISPLAY");
    const bool x11 = environment_set("DISPLAY");
    if (wayland && executable("wl-copy")) {
        return {"wl-copy"};
    }
    if (x11 && executable("xclip")) {
        return {"xclip", "-selection", "clipboard"};
    }
    if (x11 && executable("xsel")) {
        return {"xsel", "--clipboard", "--input"};
    }
    if (!wayland && !x11 && executable("wl-copy")) {
        return {"wl-copy"};
    }
    throw std::runtime_error("no clipboard command found; install wl-clipboard, xclip, or xsel");
#endif
}

} // namespace

SubmissionArtifact prepare_submission(const fs::path& root, const Problem& problem,
                                      const SubmissionOptions& options) {
    Judge judge(root);
    TestOptions test_options;
    test_options.rebuild = options.rebuild;
    test_options.concise = true;
    test_options.submission_profile = true;
    const TestSummary tests = judge.test(problem, test_options);
    if (!tests.success()) {
        throw std::runtime_error("submission stopped: saved tests failed");
    }
    if (tests.total == 0) {
        throw std::runtime_error("submission stopped: no saved test input exists");
    }

    Builder builder(root);
    const BuildResult checked =
        builder.build_problem(problem, BuildOptions{true, options.rebuild, false});
    if (read_text(tests.build.bundled_source) != read_text(checked.bundled_source)) {
        throw std::runtime_error("submission stopped: source changed while tests were running");
    }
    const std::string source = read_text(checked.bundled_source);
    const std::string hash = content_digest(source);
    const fs::path authored_path = problem.solution_path();
    const std::string authored_source = read_text(authored_path);
    if (builder.bundled_source(authored_path) != source ||
        read_text(authored_path) != authored_source) {
        throw std::runtime_error("submission stopped: source changed while snapshots were saved");
    }
    const fs::path directory = cfx::state_root(root) / "submissions" / "prepared" /
                               (problem.id() + "-" + hash.substr(0, 16));
    fs::create_directories(directory);
    const fs::path artifact = directory / "submission.cpp";
    const fs::path authored_artifact = directory / "solution.cpp";
    write_atomic(artifact, source);
    write_atomic(authored_artifact, authored_source);

    return SubmissionArtifact{
        artifact,
        source,
        hash,
        problem.id(),
        language_name(configured_standard()),
        submission_url(problem),
    };
}

void copy_submission_to_clipboard(const SubmissionArtifact& artifact) {
    const ProcessResult result =
        run_process(clipboard_command(), ProcessOptions{
                                             .stdin_path = artifact.source,
                                             .timeout = std::chrono::seconds(10),
                                         });
    if (result.status != 0) {
        throw std::runtime_error("cannot copy the tested bundle to the clipboard");
    }
}

} // namespace cfx
