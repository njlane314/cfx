#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace cfx {

class Problem;

struct BuildOptions {
    bool checked = false;
    bool local = true;
};

struct BuildResult {
    std::filesystem::path binary;
    std::filesystem::path bundled_source;
};

class Builder {
  public:
    explicit Builder(std::filesystem::path root);

    std::string bundled_source(const std::filesystem::path& source) const;

    BuildResult build_problem(const Problem& problem, const BuildOptions& options = {}) const;

  private:
    std::filesystem::path root_;
};

std::string format_bytes(std::uintmax_t bytes);
std::string configured_standard();

} // namespace cfx
