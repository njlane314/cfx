#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cfprobs {

class ProblemError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class Problem {
  public:
    Problem(std::string contest_id, std::string index,
            std::filesystem::path root = std::filesystem::current_path());

    static Problem parse(std::string_view value,
                         const std::filesystem::path& root = std::filesystem::current_path());
    static Problem parse(std::string_view index, std::string_view contest_id,
                         const std::filesystem::path& root = std::filesystem::current_path());
    static std::optional<Problem>
    infer(const std::filesystem::path& location,
          const std::filesystem::path& root = std::filesystem::current_path());

    [[nodiscard]] const std::string& contest_id() const noexcept;
    [[nodiscard]] const std::string& index() const noexcept;

    // Codeforces-native form, for example "71A".
    [[nodiscard]] std::string id() const;
    // Compatibility form used by the original flat workspace, for example "A.71".
    [[nodiscard]] std::string legacy_id() const;

    [[nodiscard]] std::filesystem::path directory() const;
    [[nodiscard]] std::filesystem::path preferred_solution_path() const;
    [[nodiscard]] std::filesystem::path legacy_solution_path() const;
    [[nodiscard]] std::filesystem::path solution_path() const;
    [[nodiscard]] std::filesystem::path samples_path() const;
    [[nodiscard]] std::filesystem::path cases_path() const;
    [[nodiscard]] std::filesystem::path stress_path() const;
    [[nodiscard]] std::filesystem::path legacy_tests_path() const;
    [[nodiscard]] std::vector<std::filesystem::path> test_directories() const;
    [[nodiscard]] bool uses_new_layout() const;

  private:
    std::string contest_id_;
    std::string index_;
    std::filesystem::path root_;
};

// Walk upward from start and return the first cf-probs repository root.
// If no marker is found, start itself is returned in normalized absolute form.
[[nodiscard]] std::filesystem::path
find_workspace_root(const std::filesystem::path& start = std::filesystem::current_path());

} // namespace cfprobs
