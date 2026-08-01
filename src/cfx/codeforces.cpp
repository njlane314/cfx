#include "codeforces.hpp"

#include "json.hpp"
#include "problem.hpp"
#include "process.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <thread>

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
        if (page != 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2100));
        }
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

std::string problem_url(const Problem& problem) {
    return "https://codeforces.com/contest/" + problem.contest_id() + "/problem/" + problem.index();
}

std::string submission_url(const Problem& problem) {
    return "https://codeforces.com/contest/" + problem.contest_id() + "/submit";
}

} // namespace cfx
