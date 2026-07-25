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

struct WorkspaceResult {
    std::filesystem::path solution;
    bool solution_created = false;
};

class Workspace {
  public:
    explicit Workspace(std::filesystem::path root = std::filesystem::current_path());

    // Creates problems/cf/<contest>/<index> without overwriting an existing
    // solution. An empty template_path selects templates/solution.cpp.
    WorkspaceResult create(const Problem& problem,
                           const std::filesystem::path& template_path = {}) const;

  private:
    std::filesystem::path root_;
};

// Store the problem selected by `cfx PROBLEM` as persistent workspace state.
void remember_current_problem(const Problem& problem,
                              const std::filesystem::path& root = std::filesystem::current_path());

// Return the last selected problem, or no value when none has been recorded.
[[nodiscard]] std::optional<Problem>
current_problem(const std::filesystem::path& root = std::filesystem::current_path());

} // namespace cfx
