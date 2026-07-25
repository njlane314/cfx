#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cfx {

class Problem;

struct BuildOptions {
    bool checked = false;
    bool rebuild = false;
    bool local = true;
};

struct BuildResult {
    std::filesystem::path binary;
    std::filesystem::path bundled_source;
    std::string digest;
    bool compiled = false;
    std::uintmax_t source_size = 0;
    std::uintmax_t binary_size = 0;
};

class Builder {
  public:
    explicit Builder(std::filesystem::path root);

    std::string bundled_source(const std::filesystem::path& source) const;

    BuildResult build_problem(const Problem& problem, const BuildOptions& options = {}) const;

    BuildResult build_source(const std::filesystem::path& source, const std::string& cache_name,
                             const BuildOptions& options = {}) const;

  private:
    std::filesystem::path root_;
};

std::string format_bytes(std::uintmax_t bytes);
std::string configured_standard();

} // namespace cfx
