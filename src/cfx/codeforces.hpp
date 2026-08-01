#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace cfx {

class Problem;

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

CodeforcesSubmission poll_submission_status(
    const std::string& contest_id, const std::string& problem_index,
    const std::string& handle, const std::string& submission_id,
    const std::chrono::steady_clock::time_point& deadline,
    std::chrono::milliseconds interval = std::chrono::milliseconds(2100));

std::string problem_url(const Problem& problem);
std::string submission_url(const Problem& problem);

} // namespace cfx
