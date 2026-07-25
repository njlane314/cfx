#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace cfprobs {

class Problem;

std::vector<std::string> parse_contest_indexes(std::string_view response);
std::vector<std::string> fetch_contest_indexes(const std::string& contest_id);

std::string submission_url(const Problem& problem);

} // namespace cfprobs
