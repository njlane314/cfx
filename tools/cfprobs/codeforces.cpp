#include "codeforces.hpp"

#include "json.hpp"
#include "problem.hpp"
#include "process.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace cfprobs {
namespace {

std::string api_base() {
    const char* override = std::getenv("CFPROBS_API_BASE");
    return override == nullptr ? "https://codeforces.com/api" : std::string(override);
}

} // namespace

std::vector<std::string> parse_contest_indexes(std::string_view response) {
    const Json document = parse_json(response);
    const Json* status = document.find("status");
    if (status == nullptr || !status->is_string() || status->string() != "OK") {
        const Json* comment = document.find("comment");
        throw std::runtime_error(comment != nullptr && comment->is_string()
                                     ? "Codeforces API: " + comment->string()
                                     : "Codeforces API returned a non-OK response");
    }

    const Json& result = document.at("result");
    const Json& problems = result.at("problems");
    std::vector<std::string> indexes;
    for (const Json& value : problems.array()) {
        const Json* index = value.find("index");
        if (index != nullptr && index->is_string() &&
            std::find(indexes.begin(), indexes.end(), index->string()) == indexes.end()) {
            indexes.push_back(index->string());
        }
    }
    if (indexes.empty()) {
        throw std::runtime_error("Codeforces API returned no contest problems");
    }
    return indexes;
}

std::vector<std::string> fetch_contest_indexes(const std::string& contest_id) {
    const std::string url = api_base() + "/contest.standings?contestId=" + contest_id;
    const CaptureResult response = capture_process({
        "curl",
        "--fail-with-body",
        "--silent",
        "--show-error",
        "--location",
        "--max-time",
        "20",
        "--user-agent",
        "cf-probs/1",
        url,
    });
    if (response.status != 0) {
        throw std::runtime_error("cannot fetch Codeforces contest " + contest_id + ":\n" +
                                 response.output);
    }
    return parse_contest_indexes(response.output);
}

std::string problem_url(const Problem& problem) {
    return "https://codeforces.com/contest/" + problem.contest_id() + "/problem/" + problem.index();
}

std::string submission_url(const Problem& problem) {
    return "https://codeforces.com/contest/" + problem.contest_id() + "/submit";
}

} // namespace cfprobs
