#include "submission.hpp"

#include "codeforces.hpp"
#include "compiler.hpp"
#include "hash.hpp"
#include "judge.hpp"
#include "problem.hpp"
#include "process.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace cfprobs {
namespace {

namespace fs = std::filesystem;

std::string read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read " + path.string());
    }
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

void write_file(const fs::path& path, const std::string& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output || !(output << value)) {
        throw std::runtime_error("cannot write " + path.string());
    }
}

std::optional<fs::path> executable(const std::string& name) {
    const char* raw_path = std::getenv("PATH");
    if (raw_path == nullptr) {
        return std::nullopt;
    }
    std::string_view path(raw_path);
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t colon = path.find(':', start);
        const std::string_view directory =
            path.substr(start, (colon == std::string_view::npos ? path.size() : colon) - start);
        const fs::path candidate = (directory.empty() ? fs::path(".") : fs::path(directory)) / name;
        std::error_code error;
        const fs::perms permissions = fs::status(candidate, error).permissions();
        if (!error && fs::is_regular_file(candidate) &&
            (permissions & (fs::perms::owner_exec | fs::perms::group_exec |
                            fs::perms::others_exec)) != fs::perms::none) {
            return candidate;
        }
        if (colon == std::string_view::npos) {
            break;
        }
        start = colon + 1;
    }
    return std::nullopt;
}

bool copy_to_clipboard(const fs::path& source) {
    for (const std::vector<std::string>& command : {
             std::vector<std::string>{"pbcopy"},
             std::vector<std::string>{"wl-copy"},
             std::vector<std::string>{"xclip", "-selection", "clipboard"},
         }) {
        if (!executable(command.front())) {
            continue;
        }
        ProcessOptions options;
        options.stdin_path = source;
        const ProcessResult result = run_process(command, options);
        if (result.status == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

SubmissionArtifact prepare_submission(const fs::path& root, const Problem& problem, bool rebuild) {
    Judge judge(root);
    const TestSummary tests =
        judge.test(problem, TestOptions{false, rebuild, std::chrono::milliseconds(5000)});
    if (!tests.success()) {
        throw std::runtime_error("submission stopped: saved tests failed");
    }
    if (tests.total == 0) {
        throw std::runtime_error("submission stopped: no complete input/output test pair exists");
    }

    Builder builder(root);
    const BuildResult checked = builder.build_problem(problem, BuildOptions{true, rebuild, false});
    if (read_file(tests.build.bundled_source) != read_file(checked.bundled_source)) {
        throw std::runtime_error("submission stopped: source changed while tests were running");
    }
    const std::string source = read_file(checked.bundled_source);
    const std::string hash = content_digest(source);
    const fs::path directory = root / ".build" / "submissions";
    fs::create_directories(directory);
    const fs::path artifact = directory / (problem.id() + "-" + hash.substr(0, 16) + ".cpp");
    write_file(artifact, source);

    return SubmissionArtifact{
        artifact,
        hash,
        problem.id(),
        "C++ (" + configured_standard() + ")",
        submission_url(problem),
    };
}

void BrowserAssistedTransport::handoff(const SubmissionArtifact& artifact,
                                       bool open_browser) const {
    if (copy_to_clipboard(artifact.source)) {
        std::cout << "copied bundled source to the clipboard\n";
    } else {
        std::cout << "clipboard helper unavailable; use " << artifact.source << '\n';
    }

    if (!open_browser) {
        std::cout << "submission page: " << artifact.page_url << '\n';
        return;
    }

    std::vector<std::string> command;
#ifdef __APPLE__
    if (executable("open")) {
        command = {"open", artifact.page_url};
    }
#else
    if (executable("xdg-open")) {
        command = {"xdg-open", artifact.page_url};
    }
#endif
    if (command.empty()) {
        std::cout << "open this submission page: " << artifact.page_url << '\n';
        return;
    }
    const ProcessResult result = run_process(command);
    if (result.status != 0) {
        throw std::runtime_error("cannot open the submission page");
    }
}

} // namespace cfprobs
