#include "cfx/problem.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <system_error>
#include <utility>

namespace cfx {
namespace {

namespace fs = std::filesystem;

const std::regex kCanonical(R"(^([0-9]+)([A-Za-z][A-Za-z0-9]*)$)");
const std::regex kLegacy(R"(^([A-Za-z][A-Za-z0-9]*)\.([0-9]+)$)");
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

std::optional<Problem> infer_new_layout(const fs::path& location, const fs::path& root) {
    const auto parts = components(location);
    for (std::size_t i = 0; i + 3 < parts.size(); ++i) {
        if (parts[i] == "problems" && parts[i + 1] == "cf" && valid_contest(parts[i + 2]) &&
            valid_index(parts[i + 3])) {
            return Problem(parts[i + 2], parts[i + 3], root);
        }
    }
    return std::nullopt;
}

std::optional<Problem> infer_legacy_layout(const fs::path& location, const fs::path& root) {
    const auto parts = components(location);
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        if (parts[i] != "solutions" && parts[i] != "tests") {
            continue;
        }
        auto name = parts[i + 1];
        if (parts[i] == "solutions" && name.ends_with(".cpp")) {
            name.resize(name.size() - 4);
        }
        std::smatch match;
        if (std::regex_match(name, match, kLegacy)) {
            return Problem(match[2].str(), match[1].str(), root);
        }
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
    if (std::regex_match(token, match, kLegacy)) {
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
    const auto normalized_location = absolute_location.lexically_normal();

    if (const auto problem = infer_new_layout(normalized_location, normalized_root)) {
        return problem;
    }
    return infer_legacy_layout(normalized_location, normalized_root);
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

std::string Problem::legacy_id() const {
    return index_ + "." + contest_id_;
}

fs::path Problem::directory() const {
    return root_ / "problems" / "cf" / contest_id_ / index_;
}

fs::path Problem::preferred_solution_path() const {
    return directory() / "solution.cpp";
}

fs::path Problem::legacy_solution_path() const {
    return root_ / "solutions" / (legacy_id() + ".cpp");
}

fs::path Problem::solution_path() const {
    if (fs::is_regular_file(preferred_solution_path())) {
        return preferred_solution_path();
    }
    if (fs::is_regular_file(legacy_solution_path())) {
        return legacy_solution_path();
    }
    return preferred_solution_path();
}

fs::path Problem::samples_path() const {
    return directory() / "samples";
}

fs::path Problem::cases_path() const {
    return directory() / "cases";
}

fs::path Problem::stress_path() const {
    return directory() / "stress";
}

fs::path Problem::legacy_tests_path() const {
    return root_ / "tests" / legacy_id();
}

std::vector<fs::path> Problem::test_directories() const {
    if (uses_new_layout()) {
        std::vector<fs::path> directories{samples_path(), cases_path()};
        if (fs::is_directory(legacy_tests_path())) {
            directories.push_back(legacy_tests_path());
        }
        return directories;
    }
    return {legacy_tests_path()};
}

bool Problem::uses_new_layout() const {
    return fs::is_regular_file(preferred_solution_path());
}

fs::path find_workspace_root(const fs::path& start) {
    auto current = normalized_absolute(start);
    std::error_code error;
    if (fs::is_regular_file(current, error)) {
        current = current.parent_path();
    }

    while (!current.empty()) {
        const bool has_git = fs::exists(current / ".git");
        const bool has_shape = fs::exists(current / "bin") && fs::exists(current / "include");
        if (has_git || has_shape) {
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
