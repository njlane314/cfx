#include "cfx.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <random>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <unistd.h>
#include <vector>

namespace cfx {
namespace {

class ApiRequestError final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

std::string api_base() {
    const char* override = std::getenv("CFX_API_BASE");
    std::string base = override == nullptr ? "https://codeforces.com/api" : override;
    while (base.ends_with('/')) {
        base.pop_back();
    }
    return base;
}

std::string query_value(std::string_view value) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string encoded;
    for (const unsigned char character : value) {
        if (std::isalnum(character) != 0 || character == '-' || character == '_' ||
            character == '.' || character == '~') {
            encoded.push_back(static_cast<char>(character));
        } else {
            encoded.push_back('%');
            encoded.push_back(digits[character >> 4U]);
            encoded.push_back(digits[character & 0x0fU]);
        }
    }
    return encoded;
}

std::string api_get(const std::string& url, int timeout_seconds) {
    if (std::getenv("CFX_API_BASE") == nullptr) {
        static auto next_request = std::chrono::steady_clock::time_point{};
        std::this_thread::sleep_until(next_request);
        next_request = std::chrono::steady_clock::now() + std::chrono::milliseconds(2100);
    }
    const CaptureResult response = capture_process({
        "curl",
        "--fail-with-body",
        "--silent",
        "--show-error",
        "--location",
        "--max-time",
        std::to_string(timeout_seconds),
        "--user-agent",
        "cfx/1",
        url,
    });
    if (response.status != 0) {
        throw ApiRequestError("cannot reach Codeforces API:\n" + response.output);
    }
    return response.output;
}

const Json& api_result(const Json& document) {
    if (!document.is_object()) {
        throw std::runtime_error("Codeforces API returned invalid data");
    }
    const Json* status = document.find("status");
    if (status == nullptr || !status->is_string() || status->string() != "OK") {
        const Json* comment = document.find("comment");
        throw std::runtime_error(comment != nullptr && comment->is_string()
                                     ? "Codeforces API: " + comment->string()
                                     : "Codeforces API returned a non-OK response");
    }
    const Json* result = document.find("result");
    if (result == nullptr) {
        throw std::runtime_error("Codeforces API returned no result");
    }
    return *result;
}

std::uint64_t nonnegative_integer(const Json& value, std::string_view name) {
    if (!value.is_number()) {
        throw std::runtime_error("Codeforces API returned an invalid " + std::string(name));
    }
    const double number = value.number();
    constexpr double maximum_safe_integer = 9007199254740991.0;
    if (!std::isfinite(number) || number < 0.0 || std::floor(number) != number ||
        number > maximum_safe_integer) {
        throw std::runtime_error("Codeforces API returned an invalid " + std::string(name));
    }
    return static_cast<std::uint64_t>(number);
}

std::uint64_t required_integer(const Json& object, std::string_view name) {
    const Json* value = object.find(name);
    if (value == nullptr) {
        throw std::runtime_error("Codeforces API returned no " + std::string(name));
    }
    return nonnegative_integer(*value, name);
}

std::uint64_t decimal_id(std::string_view value, std::string_view name) {
    std::uint64_t result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    constexpr std::uint64_t maximum_safe_integer = 9007199254740991ULL;
    if (value.empty() || error != std::errc() || end != value.data() + value.size() || result == 0 ||
        result > maximum_safe_integer) {
        throw std::invalid_argument("invalid " + std::string(name));
    }
    return result;
}

Json parse_api_response(std::string_view response) {
    const Json document = parse_json(response);
    return api_result(document);
}

Json problem_catalogue(const std::filesystem::path& root) {
    const std::filesystem::path cache = state_root(root) / "problemset.json";
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(cache, error);
    if (!error && std::filesystem::file_time_type::clock::now() - modified <
                      std::chrono::hours(24)) {
        return parse_api_response(read_text(cache));
    }

    const std::string response = api_get(api_base() + "/problemset.problems", 30);
    Json result = parse_api_response(response);
    std::filesystem::create_directories(cache.parent_path());
    write_atomic(cache, response);
    std::filesystem::last_write_time(cache, std::filesystem::file_time_type::clock::now());
    return result;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool terminal_verdict(std::string_view verdict) {
    if (verdict.empty() || verdict == "NULL" || verdict == "SUBMITTED" ||
        verdict == "TESTING") {
        return false;
    }
    return std::all_of(verdict.begin(), verdict.end(), [](unsigned char character) {
        return (character >= 'A' && character <= 'Z') || character == '_';
    });
}

std::string verdict_text(std::string_view verdict, std::string_view testset) {
    if (verdict == "OK" && testset == "PRETESTS") {
        return "Accepted (pretests)";
    }
    static const std::pair<std::string_view, std::string_view> names[] = {
        {"OK", "Accepted"},
        {"PARTIAL", "Partial"},
        {"COMPILATION_ERROR", "Compilation Error"},
        {"RUNTIME_ERROR", "Runtime Error"},
        {"WRONG_ANSWER", "Wrong Answer"},
        {"PRESENTATION_ERROR", "Presentation Error"},
        {"TIME_LIMIT_EXCEEDED", "Time Limit Exceeded"},
        {"MEMORY_LIMIT_EXCEEDED", "Memory Limit Exceeded"},
        {"IDLENESS_LIMIT_EXCEEDED", "Idleness Limit Exceeded"},
        {"SECURITY_VIOLATED", "Security Violation"},
        {"CRASHED", "Crashed"},
        {"INPUT_PREPARATION_CRASHED", "Input Preparation Crashed"},
        {"CHALLENGED", "Challenged"},
        {"SKIPPED", "Skipped"},
        {"REJECTED", "Rejected"},
        {"FAILED", "Failed"},
    };
    for (const auto& [value, text] : names) {
        if (value == verdict) {
            return std::string(text);
        }
    }
    std::string text;
    bool new_word = true;
    for (const char character : verdict) {
        if (character == '_') {
            text.push_back(' ');
            new_word = true;
        } else {
            text.push_back(new_word ? character : static_cast<char>(std::tolower(character)));
            new_word = false;
        }
    }
    return text;
}

std::string validate_submission(const Json& submission, std::uint64_t contest,
                                std::string_view problem_index, std::string_view handle) {
    if (required_integer(submission, "contestId") != contest) {
        throw std::runtime_error("Codeforces API submission has the wrong contest");
    }

    const Json* problem = submission.find("problem");
    if (problem == nullptr || !problem->is_object() ||
        required_integer(*problem, "contestId") != contest) {
        throw std::runtime_error("Codeforces API submission has an invalid problem");
    }
    const Json* index = problem->find("index");
    if (index == nullptr || !index->is_string() || index->string() != problem_index) {
        throw std::runtime_error("Codeforces API submission has the wrong problem");
    }

    const Json* author = submission.find("author");
    const Json* members = author != nullptr && author->is_object() ? author->find("members") : nullptr;
    if (members == nullptr || !members->is_array()) {
        throw std::runtime_error("Codeforces API submission has an invalid author");
    }
    const std::string expected = lower(std::string(handle));
    const bool found = std::any_of(members->array().begin(), members->array().end(),
                                   [&](const Json& member) {
                                       const Json* value = member.is_object()
                                                               ? member.find("handle")
                                                               : nullptr;
                                       return value != nullptr && value->is_string() &&
                                              lower(value->string()) == expected;
                                   });
    if (!found) {
        throw std::runtime_error("Codeforces API submission has the wrong author");
    }

    const Json* participant_type = author->find("participantType");
    if (participant_type == nullptr || !participant_type->is_string()) {
        throw std::runtime_error("Codeforces API submission has no participant type");
    }
    const std::string& value = participant_type->string();
    if (value != "CONTESTANT" && value != "PRACTICE" && value != "VIRTUAL" &&
        value != "MANAGER" && value != "OUT_OF_COMPETITION") {
        throw std::runtime_error("Codeforces API submission has an invalid participant type");
    }
    return value;
}

struct SubmissionStatusPage {
    CodeforcesSubmission status;
    std::size_t size = 0;
    std::uint64_t oldest_id = 0;
};

SubmissionStatusPage parse_submission_status_page(std::string_view response,
                                                  std::uint64_t contest,
                                                  std::string_view problem_index,
                                                  std::string_view handle,
                                                  std::uint64_t expected_id) {
    const Json document = parse_json(response);
    const Json& result = api_result(document);
    if (!result.is_array()) {
        throw std::runtime_error("Codeforces API returned invalid submission data");
    }

    SubmissionStatusPage page;
    page.size = result.array().size();
    const Json* match = nullptr;
    for (const Json& submission : result.array()) {
        if (!submission.is_object()) {
            throw std::runtime_error("Codeforces API returned an invalid submission");
        }
        const std::uint64_t id = required_integer(submission, "id");
        page.oldest_id = page.oldest_id == 0 ? id : std::min(page.oldest_id, id);
        if (id == expected_id) {
            if (match != nullptr) {
                throw std::runtime_error("Codeforces API returned duplicate submission IDs");
            }
            match = &submission;
        }
    }
    if (match == nullptr) {
        return page;
    }
    const std::string participant_type =
        validate_submission(*match, contest, problem_index, handle);

    CodeforcesSubmission& status = page.status;
    status.found = true;
    status.handle = std::string(handle);
    status.participant_type = participant_type;
    const Json* verdict = match->find("verdict");
    if (verdict == nullptr || verdict->is_null()) {
        return page;
    }
    if (!verdict->is_string()) {
        throw std::runtime_error("Codeforces API returned an invalid verdict");
    }
    status.verdict = verdict->string();
    status.terminal = terminal_verdict(status.verdict);
    if (!status.terminal) {
        if (status.verdict != "" && status.verdict != "NULL" && status.verdict != "SUBMITTED" &&
            status.verdict != "TESTING") {
            throw std::runtime_error("Codeforces API returned an invalid verdict");
        }
        return page;
    }

    const Json* testset = match->find("testset");
    if (testset == nullptr || !testset->is_string()) {
        throw std::runtime_error("Codeforces API returned no testset");
    }
    status.testset = testset->string();
    status.verdict_text = verdict_text(status.verdict, testset->string());
    status.passed_test_count = required_integer(*match, "passedTestCount");
    status.time_consumed_millis = required_integer(*match, "timeConsumedMillis");
    status.memory_consumed_bytes = required_integer(*match, "memoryConsumedBytes");
    return page;
}

CodeforcesSubmission fetch_submission_status(const std::string& contest_id,
                                              const std::string& problem_index,
                                              const std::string& handle,
                                              const std::string& submission_id,
                                              int timeout_seconds) {
    constexpr int page_size = 50;
    constexpr int page_limit = 20;
    const std::uint64_t expected_id = decimal_id(submission_id, "submission ID");
    const std::uint64_t contest = decimal_id(contest_id, "contest ID");
    for (int page = 0; page < page_limit; ++page) {
        const std::string url = api_base() + "/contest.status?contestId=" +
                                query_value(contest_id) + "&handle=" + query_value(handle) +
                                "&from=" + std::to_string(page * page_size + 1) +
                                "&count=" + std::to_string(page_size);
        const std::string response = api_get(url, timeout_seconds);
        SubmissionStatusPage status = parse_submission_status_page(
            response, contest, problem_index, handle, expected_id);
        if (status.status.found || status.size < page_size || status.oldest_id < expected_id) {
            return status.status;
        }
    }
    return {};
}

} // namespace

CodeforcesSubmission poll_submission_status(
    const std::string& contest_id, const std::string& problem_index,
    const std::string& handle, const std::string& submission_id,
    const std::chrono::steady_clock::time_point& deadline, std::chrono::milliseconds interval) {
    if (interval <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("Codeforces poll interval must be positive");
    }

    std::string last_status = "waiting for the submission to appear in the Codeforces API";
    std::this_thread::sleep_until(std::min(deadline, std::chrono::steady_clock::now() + interval));
    while (std::chrono::steady_clock::now() < deadline) {
        const auto request_started = std::chrono::steady_clock::now();
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - request_started);
        const int timeout_seconds = static_cast<int>(
            std::max<std::int64_t>(1, std::min<std::int64_t>(20, (remaining.count() + 999) / 1000)));
        try {
            CodeforcesSubmission status = fetch_submission_status(
                contest_id, problem_index, handle, submission_id, timeout_seconds);
            if (status.terminal) {
                return status;
            }
            last_status = status.found ? "Codeforces is still judging"
                                       : "waiting for the submission to appear in the Codeforces API";
        } catch (const ApiRequestError& error) {
            last_status = error.what();
        }

        const auto next = std::min(deadline, std::chrono::steady_clock::now() + interval);
        std::this_thread::sleep_until(next);
    }
    throw std::runtime_error(last_status);
}

ProblemSuggestion pick_problem(const std::filesystem::path& root, const std::string& handle) {
    const Json users = parse_api_response(
        api_get(api_base() + "/user.info?handles=" + query_value(handle), 30));
    if (!users.is_array() || users.array().size() != 1) {
        throw std::runtime_error("Codeforces API returned invalid user data");
    }
    const Json& user = users.array().front();
    const Json* current_rating = user.find("rating");
    if (current_rating == nullptr) throw std::runtime_error(handle + " has no Codeforces rating");
    const std::uint64_t rating = nonnegative_integer(*current_rating, "user rating");
    const std::uint64_t target = std::max<std::uint64_t>(800, ((rating + 199) / 100) * 100);

    const Json catalogue = problem_catalogue(root);
    const Json submissions = parse_api_response(
        api_get(api_base() + "/user.status?handle=" + query_value(user.at("handle").string()), 30));
    if (!submissions.is_array()) {
        throw std::runtime_error("Codeforces API returned invalid submission data");
    }

    std::unordered_set<std::string> solved;
    for (const Json& submission : submissions.array()) {
        const Json* verdict = submission.find("verdict");
        const Json* problem = submission.find("problem");
        if (verdict == nullptr || !verdict->is_string() || verdict->string() != "OK" ||
            problem == nullptr) continue;
        const Json* contest = problem->find("contestId");
        if (contest != nullptr) {
            solved.insert(std::to_string(nonnegative_integer(*contest, "contest ID")) +
                          problem->at("index").string());
        }
    }

    std::vector<ProblemSuggestion> candidates;
    std::uint64_t rung = 0;
    for (const Json& value : catalogue.at("problems").array()) {
        const Json* contest = value.find("contestId");
        const Json* problem_rating = value.find("rating");
        if (contest == nullptr || problem_rating == nullptr ||
            value.at("type").string() != "PROGRAMMING") {
            continue;
        }
        const std::uint64_t difficulty = nonnegative_integer(*problem_rating, "rating");
        if (difficulty < target || (rung != 0 && difficulty > rung)) continue;
        const std::string& index = value.at("index").string();
        if (index.empty() || index.front() < 'A' || index.front() > 'Z' ||
            index.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") != std::string::npos) continue;

        Problem problem(std::to_string(nonnegative_integer(*contest, "contest ID")),
                        index, root);
        if (solved.contains(problem.id()) || std::filesystem::exists(problem.directory())) continue;
        if (rung == 0 || difficulty < rung) {
            rung = difficulty;
            candidates.clear();
        }
        candidates.push_back({std::move(problem), value.at("name").string(), difficulty});
    }

    if (candidates.empty()) {
        throw std::runtime_error("no eligible Codeforces ladder problem is available");
    }
    std::shuffle(candidates.begin(), candidates.end(), std::mt19937{std::random_device{}()});
    return std::move(candidates.front());
}

std::string problem_url(const Problem& problem) {
    return "https://codeforces.com/contest/" + problem.contest_id() + "/problem/" + problem.index();
}

std::string submission_url(const Problem& problem) {
    return "https://codeforces.com/contest/" + problem.contest_id() + "/submit";
}

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

SubmissionArtifact prepare_submission(const fs::path& root, const Problem& problem) {
    Judge judge(root);
    TestOptions test_options;
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
    const BuildResult checked = builder.build_problem(problem, BuildOptions{true, false});
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
