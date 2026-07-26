#include "cfx/problem.hpp"

#include "cfx/runtime.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <system_error>
#include <utility>

namespace cfx {
namespace {

namespace fs = std::filesystem;

const std::regex kCanonical(R"(^([0-9]+)([A-Za-z][A-Za-z0-9]*)$)");
const std::regex kAlternate(R"(^([A-Za-z][A-Za-z0-9]*)\.([0-9]+)$)");
const std::regex
    kProblemsetUrl(R"(codeforces\.com/problemset/problem/([0-9]+)/([A-Za-z][A-Za-z0-9]*))",
                   std::regex::icase);
const std::regex kContestUrl(R"(codeforces\.com/contest/([0-9]+)/problem/([A-Za-z][A-Za-z0-9]*))",
                             std::regex::icase);

std::string trim(std::string_view value) {
    auto first = value.begin();
    auto last = value.end();
    while (first != last && std::isspace(static_cast<unsigned char>(*first)) != 0) {
        ++first;
    }
    while (first != last && std::isspace(static_cast<unsigned char>(*(last - 1))) != 0) {
        --last;
    }
    return {first, last};
}

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](char c) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    });
    return value;
}

bool valid_contest(std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    });
}

bool valid_index(std::string_view value) {
    if (value.empty() || std::isalpha(static_cast<unsigned char>(value.front())) == 0) {
        return false;
    }
    return std::all_of(value.begin() + 1, value.end(),
                       [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; });
}

fs::path normalized_absolute(const fs::path& path) {
    std::error_code error;
    auto absolute = fs::absolute(path, error);
    if (error) {
        throw ProblemError("cannot resolve path '" + path.string() + "': " + error.message());
    }
    auto canonical = fs::weakly_canonical(absolute, error);
    return error ? absolute.lexically_normal() : canonical;
}

std::vector<std::string> components(const fs::path& path) {
    std::vector<std::string> result;
    for (const auto& part : path) {
        result.push_back(part.string());
    }
    return result;
}

bool is_beneath(const fs::path& path, const fs::path& root) {
    auto path_part = path.begin();
    for (auto root_part = root.begin(); root_part != root.end(); ++root_part, ++path_part) {
        if (path_part == path.end() || *path_part != *root_part) {
            return false;
        }
    }
    return true;
}

std::optional<Problem> infer_canonical_layout(const fs::path& location, const fs::path& root) {
    const auto parts = components(location.lexically_relative(root));
    if (parts.size() >= 3 && parts[0] == "codeforces" && valid_contest(parts[1]) &&
        valid_index(parts[2])) {
        return Problem(parts[1], parts[2], root);
    }
    return std::nullopt;
}

} // namespace

Problem::Problem(std::string contest_id, std::string index, fs::path root)
    : contest_id_(std::move(contest_id)), index_(upper(std::move(index))),
      root_(normalized_absolute(root)) {
    if (!valid_contest(contest_id_) || !valid_index(index_)) {
        throw ProblemError("invalid Codeforces problem; expected a numeric contest and an "
                           "index beginning with a letter");
    }
}

Problem Problem::parse(std::string_view value, const fs::path& root) {
    const auto token = trim(value);
    std::smatch match;
    if (std::regex_match(token, match, kCanonical)) {
        return Problem(match[1].str(), match[2].str(), root);
    }
    if (std::regex_match(token, match, kAlternate)) {
        return Problem(match[2].str(), match[1].str(), root);
    }
    if (std::regex_search(token, match, kProblemsetUrl) ||
        std::regex_search(token, match, kContestUrl)) {
        return Problem(match[1].str(), match[2].str(), root);
    }

    fs::path possible_path(token);
    if (possible_path.has_parent_path()) {
        if (const auto inferred = infer(possible_path, root)) {
            return *inferred;
        }
    }

    throw ProblemError("cannot parse problem '" + token +
                       "'; expected 71A, A.71, A 71, a Codeforces URL, or a problem path");
}

Problem Problem::parse(std::string_view index, std::string_view contest_id, const fs::path& root) {
    const auto clean_index = trim(index);
    const auto clean_contest = trim(contest_id);
    if (!valid_index(clean_index) || !valid_contest(clean_contest)) {
        throw ProblemError("cannot parse problem; expected two arguments like A 71");
    }
    return Problem(clean_contest, clean_index, root);
}

std::optional<Problem> Problem::infer(const fs::path& location, const fs::path& root) {
    const auto normalized_root = normalized_absolute(root);
    const auto absolute_location = location.is_absolute() ? location : normalized_root / location;
    const auto normalized_location = normalized_absolute(absolute_location);
    if (!is_beneath(normalized_location, normalized_root)) {
        return std::nullopt;
    }
    return infer_canonical_layout(normalized_location, normalized_root);
}

const std::string& Problem::contest_id() const noexcept {
    return contest_id_;
}

const std::string& Problem::index() const noexcept {
    return index_;
}

std::string Problem::id() const {
    return contest_id_ + index_;
}

fs::path Problem::directory() const {
    return root_ / "codeforces" / contest_id_ / index_;
}

fs::path Problem::solution_path() const {
    return directory() / "solution.cpp";
}

fs::path Problem::metadata_path() const {
    return directory() / "problem.json";
}

fs::path Problem::state_directory() const {
    return cfx::state_root(root_) / "codeforces" / contest_id_ / index_;
}

fs::path Problem::samples_path() const {
    return state_directory() / "samples";
}

fs::path Problem::cases_path() const {
    return directory() / "cases";
}

fs::path Problem::stress_path() const {
    return directory() / "stress";
}

std::vector<fs::path> Problem::test_directories() const {
    return {samples_path(), cases_path()};
}

fs::path find_workspace_root(const fs::path& start) {
    auto current = normalized_absolute(start);
    std::error_code error;
    if (fs::is_regular_file(current, error)) {
        current = current.parent_path();
    }

    while (!current.empty()) {
        const bool has_git = fs::exists(current / ".git");
        const bool has_archive = fs::exists(current / "codeforces");
        if (has_git || has_archive) {
            return current;
        }
        const auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return normalized_absolute(start);
}

} // namespace cfx
