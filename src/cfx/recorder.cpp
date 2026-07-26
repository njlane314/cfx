#include "recorder.hpp"

#include "browser.hpp"
#include "file.hpp"
#include "json.hpp"
#include "problem.hpp"
#include "process.hpp"
#include "runtime.hpp"
#include "submission.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace cfx {
namespace {

namespace fs = std::filesystem;

std::string trim_line(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

CaptureResult git_capture(const fs::path& root, std::vector<std::string> arguments) {
    arguments.insert(arguments.begin(), {"git", "-C", root.string()});
    return capture_process(arguments);
}

ProcessResult git_run(const fs::path& root, std::vector<std::string> arguments) {
    arguments.insert(arguments.begin(), {"git", "-C", root.string()});
    return run_process(arguments);
}

void preserve(const fs::path& path, std::string_view contents) {
    if (fs::exists(path)) {
        if (!fs::is_regular_file(path) || read_text(path) != contents) {
            throw std::runtime_error("refusing to replace acceptance artifact: " + path.string());
        }
        return;
    }
    fs::create_directories(path.parent_path());
    write_atomic(path, contents);
}

std::string receipt_json(const Problem& problem, const SubmissionArtifact& artifact,
                         const BrowserSubmitReceipt& receipt) {
    return "{\n"
           "  \"schemaVersion\": 1,\n"
           "  \"platform\": \"codeforces\",\n"
           "  \"problem\": " + json_quote(problem.id()) + ",\n"
           "  \"submissionId\": " + json_quote(receipt.submission_id) + ",\n"
           "  \"submissionUrl\": " + json_quote(receipt.submission_url) + ",\n"
           "  \"verdict\": " + json_quote(receipt.verdict) + ",\n"
           "  \"language\": " + json_quote(artifact.language) + ",\n"
           "  \"passedTests\": " + std::to_string(receipt.passed_test_count) + ",\n"
           "  \"timeMs\": " + std::to_string(receipt.time_consumed_millis) + ",\n"
           "  \"memoryBytes\": " + std::to_string(receipt.memory_consumed_bytes) + ",\n"
           "  \"sourceDigest\": " + json_quote(artifact.source_hash) + ",\n"
           "  \"authoredSourceDigest\": " + json_quote(artifact.authored_source_hash) + "\n"
           "}\n";
}

std::string title(const Problem& problem) {
    if (!fs::is_regular_file(problem.metadata_path())) {
        return {};
    }
    try {
        const Json document = parse_json(read_text(problem.metadata_path()));
        const Json* value = document.find("name");
        if (value == nullptr || !value->is_string()) {
            return {};
        }
        std::string result = value->string();
        std::replace_if(result.begin(), result.end(), [](unsigned char character) {
            return std::iscntrl(character) != 0;
        }, ' ');
        return result;
    } catch (const std::exception&) {
        return {};
    }
}

bool recording_enabled(const fs::path& root) {
    if (!fs::exists(root / ".git")) {
        return false;
    }
    const CaptureResult config = git_capture(root, {"config", "--get", "cfx.record"});
    const std::string value = trim_line(config.output);
    if (config.status != 0 || value == "off") {
        return false;
    }
    if (value != "commit") {
        throw std::runtime_error("git config cfx.record must be 'commit' or 'off'");
    }
    return true;
}

std::string commit_acceptance(const fs::path& root, const Problem& problem,
                              const SubmissionArtifact& artifact,
                              const BrowserSubmitReceipt& receipt) {
    const CaptureResult top = git_capture(root, {"rev-parse", "--show-toplevel"});
    std::error_code error;
    if (top.status != 0 ||
        fs::weakly_canonical(trim_line(top.output), error) != fs::weakly_canonical(root)) {
        throw std::runtime_error("solution archive root is not the Git top level");
    }
    if (read_text(problem.solution_path()) != artifact.authored_source_text) {
        throw std::runtime_error("solution changed while Codeforces was judging");
    }
    const ProcessResult staged = git_run(root, {"diff", "--cached", "--quiet"});
    if (staged.status == 1) {
        throw std::runtime_error("Git index has staged changes; commit or unstage them first");
    }
    if (staged.status != 0) {
        throw std::runtime_error("cannot inspect the Git index");
    }

    if (artifact.source_text != artifact.authored_source_text) {
        const fs::path path =
            problem.directory() / "submissions" / (receipt.submission_id + ".cpp");
        preserve(path, artifact.source_text);
    }

    const std::string problem_path =
        fs::relative(problem.directory(), root).generic_string();
    if (git_run(root, {"add", "-A", "--", problem_path}).status != 0) {
        throw std::runtime_error("cannot stage accepted solution");
    }

    std::string subject = "Solve Codeforces " + problem.id();
    if (const std::string name = title(problem); !name.empty()) {
        subject += " — " + name;
    }
    const std::string body =
        "Codeforces-Submission: " + receipt.submission_id + "\n" +
        "Codeforces-URL: " + receipt.submission_url + "\n" +
        "Codeforces-Time-Ms: " + std::to_string(receipt.time_consumed_millis) + "\n" +
        "Codeforces-Memory-Bytes: " + std::to_string(receipt.memory_consumed_bytes) + "\n" +
        "CFX-Source-Digest: " + artifact.source_hash;
    if (git_run(root, {"commit", "--allow-empty", "-m", subject, "-m", body}).status != 0) {
        (void)git_run(root, {"reset", "-q", "HEAD", "--", problem_path});
        throw std::runtime_error("Git acceptance commit failed");
    }
    const CaptureResult commit = git_capture(root, {"rev-parse", "HEAD"});
    if (commit.status != 0) {
        throw std::runtime_error("cannot read the acceptance commit ID");
    }
    return trim_line(commit.output);
}

} // namespace

AcceptanceRecord record_acceptance(const fs::path& archive_root, const Problem& problem,
                                   const SubmissionArtifact& artifact,
                                   const BrowserSubmitReceipt& receipt) {
    if (receipt.verdict != "OK" || receipt.submission_id.empty() ||
        !std::all_of(receipt.submission_id.begin(), receipt.submission_id.end(),
                     [](unsigned char character) { return std::isdigit(character) != 0; })) {
        throw std::runtime_error("only a confirmed Accepted submission can be recorded");
    }

    const fs::path directory = cfx::state_root(archive_root) / "receipts" / problem.id() /
                               receipt.submission_id;
    fs::create_directories(directory);
    preserve(directory / "solution.cpp", artifact.authored_source_text);
    preserve(directory / "submission.cpp", artifact.source_text);
    const fs::path receipt_path = directory / "receipt.json";
    write_atomic(receipt_path, receipt_json(problem, artifact, receipt));

    AcceptanceRecord result{
        .receipt = receipt_path,
        .commit = std::nullopt,
    };
    if (recording_enabled(archive_root)) {
        result.commit = commit_acceptance(archive_root, problem, artifact, receipt);
        write_atomic(directory / "git-commit", *result.commit + "\n");
    }
    return result;
}

} // namespace cfx
