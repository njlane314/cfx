#pragma once

#include "cfprobs/problem.hpp"

#include <filesystem>
#include <stdexcept>

namespace cfprobs {

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
    // solution. A legacy flat solution is copied when present; otherwise an
    // empty template_path selects templates/solution.cpp.
    WorkspaceResult create(const Problem& problem,
                           const std::filesystem::path& template_path = {}) const;

  private:
    std::filesystem::path root_;
};

} // namespace cfprobs
