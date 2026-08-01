#pragma once

#include "cfx/problem.hpp"

#include <filesystem>
#include <optional>
#include <stdexcept>

namespace cfx {

class WorkspaceError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class Workspace {
  public:
    explicit Workspace(std::filesystem::path root = std::filesystem::current_path());

    // Creates codeforces/<contest>/<index> without overwriting an existing
    // solution. A repository template overrides the packaged template.
    std::filesystem::path create(const Problem& problem) const;

  private:
    std::filesystem::path root_;
};

// Store the problem selected by `cfx PROBLEM` as persistent runtime state.
void remember_current_problem(const Problem& problem,
                              const std::filesystem::path& root = std::filesystem::current_path());

// Return the last selected problem, or no value when none has been recorded.
[[nodiscard]] std::optional<Problem>
current_problem(const std::filesystem::path& root = std::filesystem::current_path());

} // namespace cfx
