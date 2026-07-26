#include "recorder.hpp"

#include "browser.hpp"
#include "codeforces.hpp"
#include "file.hpp"
#include "hash.hpp"
#include "json.hpp"
#include "problem.hpp"
#include "process.hpp"
#include "runtime.hpp"
#include "submission.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
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
            throw std::runtime_error("refusing to replace submission artifact: " + path.string());
        }
        return;
    }
    fs::create_directories(path.parent_path());
    write_atomic(path, contents);
}

bool digits(std::string_view value) {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isdigit(character) != 0;
           });
}

bool valid_handle(std::string_view value) {
    return !value.empty() && value.size() <= 64 &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isalnum(character) != 0 || character == '_' || character == '-' ||
                      character == '.';
           });
}

std::string_view state_name(SubmissionState state) {
    switch (state) {
    case SubmissionState::pending: return "pending";
    case SubmissionState::pretests: return "pretests";
    case SubmissionState::accepted: return "accepted";
    case SubmissionState::rejected: return "rejected";
    }
    throw std::runtime_error("invalid submission state");
}

SubmissionState parse_state(std::string_view value) {
    if (value == "pending") return SubmissionState::pending;
    if (value == "pretests") return SubmissionState::pretests;
    if (value == "accepted") return SubmissionState::accepted;
    if (value == "rejected") return SubmissionState::rejected;
    throw std::runtime_error("invalid stored submission state");
}

SubmissionState submission_state(const BrowserSubmitReceipt& receipt) {
    if (receipt.verdict.empty()) return SubmissionState::pending;
    if (receipt.verdict == "OK") {
        if (receipt.testset == "TESTS") return SubmissionState::accepted;
        if (receipt.testset == "PRETESTS") return SubmissionState::pretests;
        return SubmissionState::pending;
    }
    return SubmissionState::rejected;
}

std::string receipt_json(const Problem& problem, const SubmissionArtifact& artifact,
                         const BrowserSubmitReceipt& receipt, SubmissionState state) {
    return "{\n"
           "  \"schemaVersion\": 2,\n"
           "  \"platform\": \"codeforces\",\n"
           "  \"problem\": " + json_quote(problem.id()) + ",\n"
           "  \"submissionId\": " + json_quote(receipt.submission_id) + ",\n"
           "  \"submissionUrl\": " + json_quote(receipt.submission_url) + ",\n"
           "  \"handle\": " + json_quote(receipt.handle) + ",\n"
           "  \"participantType\": " + json_quote(receipt.participant_type) + ",\n"
           "  \"testset\": " + json_quote(receipt.testset) + ",\n"
           "  \"verdict\": " + json_quote(receipt.verdict) + ",\n"
           "  \"verdictText\": " + json_quote(receipt.verdict_text) + ",\n"
           "  \"state\": " + json_quote(state_name(state)) + ",\n"
           "  \"pendingReason\": " + json_quote(receipt.pending_reason) + ",\n"
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
        throw std::runtime_error("solution changed after submission");
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

bool message_has_line(std::string_view message, std::string_view expected) {
    for (std::size_t begin = 0; begin <= message.size();) {
        const std::size_t end = message.find('\n', begin);
        if (message.substr(begin, end - begin) == expected) return true;
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return false;
}

std::optional<std::string> find_acceptance_commit(const fs::path& root,
                                                  const SubmissionArtifact& artifact,
                                                  const BrowserSubmitReceipt& receipt) {
    if (git_capture(root, {"rev-parse", "--verify", "HEAD"}).status != 0) {
        return std::nullopt;
    }
    const CaptureResult log = git_capture(root, {"log", "-z", "--format=%H%x00%B", "HEAD"});
    if (log.status != 0) {
        throw std::runtime_error("cannot inspect acceptance commits");
    }
    const std::string submission = "Codeforces-Submission: " + receipt.submission_id;
    const std::string digest = "CFX-Source-Digest: " + artifact.source_hash;
    std::size_t begin = 0;
    while (begin < log.output.size()) {
        const std::size_t hash_end = log.output.find('\0', begin);
        if (hash_end == std::string::npos) break;
        const std::size_t message_end = log.output.find('\0', hash_end + 1);
        if (message_end == std::string::npos) break;
        const std::string_view message(log.output.data() + hash_end + 1,
                                       message_end - hash_end - 1);
        if (message_has_line(message, submission) && message_has_line(message, digest)) {
            return log.output.substr(begin, hash_end - begin);
        }
        begin = message_end + 1;
    }
    return std::nullopt;
}

std::optional<std::string> record_acceptance(const fs::path& root, const Problem& problem,
                                             const SubmissionArtifact& artifact,
                                             const BrowserSubmitReceipt& receipt,
                                             const fs::path& directory) {
    if (!recording_enabled(root)) return std::nullopt;
    std::optional<std::string> commit = find_acceptance_commit(root, artifact, receipt);
    if (!commit) {
        commit = commit_acceptance(root, problem, artifact, receipt);
    }
    write_atomic(directory / "git-commit", *commit + "\n");
    return commit;
}

const std::string& required_string(const Json& document, std::string_view name) {
    const Json* value = document.find(name);
    if (value == nullptr || !value->is_string()) {
        throw std::runtime_error("stored submission has no " + std::string(name));
    }
    return value->string();
}

std::uint64_t required_integer(const Json& document, std::string_view name) {
    const Json* value = document.find(name);
    constexpr double maximum_safe_integer = 9007199254740991.0;
    if (value == nullptr || !value->is_number() || !std::isfinite(value->number()) ||
        value->number() < 0.0 || std::floor(value->number()) != value->number() ||
        value->number() > maximum_safe_integer) {
        throw std::runtime_error("stored submission has an invalid " + std::string(name));
    }
    return static_cast<std::uint64_t>(value->number());
}

bool schema_two(const Json& document) {
    const Json* schema = document.find("schemaVersion");
    return schema != nullptr && schema->is_number() && std::isfinite(schema->number()) &&
           schema->number() == 2.0;
}

Json read_receipt(const fs::path& path) {
    const Json document = parse_json(read_text(path));
    if (!document.is_object() || !schema_two(document)) {
        throw std::runtime_error("unsupported stored submission receipt: " + path.string());
    }
    return document;
}

SubmissionArtifact load_artifact(const StoredSubmission& stored, const Problem& problem) {
    const Json receipt = read_receipt(stored.directory / "receipt.json");
    const fs::path source = stored.directory / "submission.cpp";
    const fs::path authored = stored.directory / "solution.cpp";
    const std::string source_text = read_text(source);
    const std::string authored_text = read_text(authored);
    const std::string source_hash = content_digest(source_text);
    const std::string authored_hash = content_digest(authored_text);
    if (source_hash != required_string(receipt, "sourceDigest") ||
        authored_hash != required_string(receipt, "authoredSourceDigest")) {
        throw std::runtime_error("stored submission source digest does not match");
    }
    return SubmissionArtifact{
        source,
        source_text,
        source_hash,
        authored,
        authored_text,
        authored_hash,
        problem.id(),
        required_string(receipt, "language"),
        submission_url(problem),
    };
}

BrowserSubmitReceipt load_receipt(const StoredSubmission& stored) {
    const Json document = read_receipt(stored.directory / "receipt.json");
    BrowserSubmitReceipt receipt;
    receipt.submission_url = required_string(document, "submissionUrl");
    receipt.submission_id = required_string(document, "submissionId");
    receipt.handle = required_string(document, "handle");
    receipt.participant_type = required_string(document, "participantType");
    receipt.testset = required_string(document, "testset");
    receipt.verdict = required_string(document, "verdict");
    receipt.verdict_text = required_string(document, "verdictText");
    receipt.pending_reason = required_string(document, "pendingReason");
    receipt.passed_test_count = required_integer(document, "passedTests");
    receipt.time_consumed_millis = required_integer(document, "timeMs");
    receipt.memory_consumed_bytes = required_integer(document, "memoryBytes");
    if (receipt.submission_id != stored.submission_id || receipt.handle != stored.handle ||
        parse_state(required_string(document, "state")) != stored.state ||
        submission_state(receipt) != stored.state) {
        throw std::runtime_error("stored submission receipt is inconsistent");
    }
    return receipt;
}

RecordedSubmission persist_submission(const fs::path& root, const Problem& problem,
                                      const SubmissionArtifact& artifact,
                                      const BrowserSubmitReceipt& receipt) {
    if (!digits(receipt.submission_id) || !valid_handle(receipt.handle)) {
        throw std::runtime_error("submission receipt has no confirmed identity");
    }
    const SubmissionState state = submission_state(receipt);
    const fs::path directory = state_root(root) / "receipts" / problem.id() /
                               receipt.submission_id;
    fs::create_directories(directory);
    preserve(directory / "solution.cpp", artifact.authored_source_text);
    preserve(directory / "submission.cpp", artifact.source_text);
    const fs::path receipt_path = directory / "receipt.json";
    write_atomic(receipt_path, receipt_json(problem, artifact, receipt, state));

    RecordedSubmission result{receipt_path, std::nullopt, state};
    if (state == SubmissionState::accepted) {
        result.commit = record_acceptance(root, problem, artifact, receipt, directory);
    }
    return result;
}

} // namespace

RecordedSubmission record_submission(const fs::path& archive_root, const Problem& problem,
                                     const SubmissionArtifact& artifact,
                                     const BrowserSubmitReceipt& receipt) {
    return persist_submission(archive_root, problem, artifact, receipt);
}

std::vector<StoredSubmission> unresolved_submissions(const fs::path& archive_root) {
    std::vector<StoredSubmission> result;
    const fs::path root = state_root(archive_root) / "receipts";
    if (!fs::is_directory(root)) {
        return result;
    }
    const bool commits = recording_enabled(archive_root);
    for (const fs::directory_entry& problem_entry : fs::directory_iterator(root)) {
        if (!problem_entry.is_directory()) continue;
        for (const fs::directory_entry& submission_entry :
             fs::directory_iterator(problem_entry.path())) {
            if (!submission_entry.is_directory()) continue;
            const fs::path directory = submission_entry.path();
            const fs::path receipt_path = directory / "receipt.json";
            if (!fs::is_regular_file(receipt_path)) continue;
            const Json receipt = parse_json(read_text(receipt_path));
            if (!receipt.is_object() || !schema_two(receipt)) continue;
            const std::string problem = required_string(receipt, "problem");
            const std::string submission = required_string(receipt, "submissionId");
            const std::string handle = required_string(receipt, "handle");
            const SubmissionState state = parse_state(required_string(receipt, "state"));
            if (problem != problem_entry.path().filename().string() ||
                submission != directory.filename().string() || !digits(submission) ||
                !valid_handle(handle)) {
                throw std::runtime_error("stored submission path does not match its receipt");
            }
            const bool needs_verdict =
                state == SubmissionState::pending || state == SubmissionState::pretests;
            const bool needs_commit = state == SubmissionState::accepted && commits &&
                                      !fs::is_regular_file(directory / "git-commit");
            if (needs_verdict || needs_commit) {
                result.push_back({problem, submission, handle, state, directory});
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return std::tie(left.problem_id, left.submission_id) <
               std::tie(right.problem_id, right.submission_id);
    });
    return result;
}

RecordedSubmission update_submission(const fs::path& archive_root,
                                     const StoredSubmission& stored,
                                     const CodeforcesSubmission& status) {
    const fs::path receipt_path = stored.directory / "receipt.json";
    if (!status.found || !status.terminal) {
        return {receipt_path, std::nullopt, stored.state};
    }
    const Problem problem = Problem::parse(stored.problem_id, archive_root);
    const SubmissionArtifact artifact = load_artifact(stored, problem);
    BrowserSubmitReceipt receipt;
    receipt.submission_url = "https://codeforces.com/contest/" + problem.contest_id() +
                             "/submission/" + stored.submission_id;
    receipt.submission_id = stored.submission_id;
    receipt.handle = status.handle.empty() ? stored.handle : status.handle;
    receipt.participant_type = status.participant_type;
    receipt.testset = status.testset;
    receipt.verdict = status.verdict;
    receipt.verdict_text = status.verdict_text;
    receipt.passed_test_count = status.passed_test_count;
    receipt.time_consumed_millis = status.time_consumed_millis;
    receipt.memory_consumed_bytes = status.memory_consumed_bytes;
    return persist_submission(archive_root, problem, artifact, receipt);
}

RecordedSubmission retry_submission_recording(const fs::path& archive_root,
                                              const StoredSubmission& stored) {
    if (stored.state != SubmissionState::accepted) {
        throw std::runtime_error("stored submission is not accepted");
    }
    const Problem problem = Problem::parse(stored.problem_id, archive_root);
    const SubmissionArtifact artifact = load_artifact(stored, problem);
    const BrowserSubmitReceipt receipt = load_receipt(stored);
    return {
        stored.directory / "receipt.json",
        record_acceptance(archive_root, problem, artifact, receipt, stored.directory),
        stored.state,
    };
}

} // namespace cfx
