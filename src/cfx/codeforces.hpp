#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cfx {

class Problem;

std::vector<std::string> parse_contest_indexes(std::string_view response);
std::vector<std::string> fetch_contest_indexes(const std::string& contest_id);

struct CodeforcesSubmission {
    bool found = false;
    bool terminal = false;
    std::string verdict;
    std::string verdict_text;
    std::string handle;
    std::string participant_type;
    std::string testset;
    std::uint64_t passed_test_count = 0;
    std::uint64_t time_consumed_millis = 0;
    std::uint64_t memory_consumed_bytes = 0;
};

CodeforcesSubmission parse_submission_status(std::string_view response,
                                             const std::string& contest_id,
                                             const std::string& problem_index,
                                             const std::string& handle,
                                             const std::string& submission_id);
CodeforcesSubmission fetch_submission_status(const std::string& contest_id,
                                             const std::string& problem_index,
                                             const std::string& handle,
                                             const std::string& submission_id,
                                             int timeout_seconds = 20);
CodeforcesSubmission poll_submission_status(
    const std::string& contest_id, const std::string& problem_index,
    const std::string& handle, const std::string& submission_id,
    const std::chrono::steady_clock::time_point& deadline,
    std::chrono::milliseconds interval = std::chrono::milliseconds(2100));

std::string problem_url(const Problem& problem);
std::string submission_url(const Problem& problem);

} // namespace cfx
