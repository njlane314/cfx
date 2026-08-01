#pragma once

#include "compiler.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cfx {

class Problem;

struct ProblemLimits {
    std::chrono::milliseconds time_limit{5000};
    std::optional<std::uint64_t> memory_limit_bytes;
    bool time_from_metadata = false;
    bool memory_from_metadata = false;
};

struct TestOptions {
    bool checked = false;
    bool rebuild = false;
    std::optional<std::chrono::milliseconds> timeout;
    bool concise = false;
    std::optional<std::uint64_t> memory_limit_bytes;
    std::optional<std::uint64_t> output_limit_bytes{64U * 1024U * 1024U};
    bool submission_profile = false;
};

struct TestSummary {
    BuildResult build;
    int passed = 0;
    int total = 0;

    [[nodiscard]] bool success() const noexcept {
        return passed == total;
    }
};

struct StressOptions {
    std::filesystem::path generator;
    std::filesystem::path brute;
    int iterations = 100;
    std::uint64_t seed = 1;
    std::vector<std::string> generator_arguments;
    bool checked = false;
    bool rebuild = false;
    bool verbose = false;
    std::chrono::milliseconds timeout{5000};
    std::chrono::milliseconds generator_timeout{5000};
};

struct StressSummary {
    int completed = 0;
    std::optional<std::uint64_t> failing_seed;

    [[nodiscard]] bool success() const noexcept {
        return !failing_seed.has_value();
    }
};

class Judge {
  public:
    explicit Judge(std::filesystem::path root);

    TestSummary test(const Problem& problem, const TestOptions& options = {}) const;

    StressSummary stress(const Problem& problem, const StressOptions& options) const;

    std::pair<std::filesystem::path, std::filesystem::path>
    promote_failure(const Problem& problem) const;

  private:
    std::filesystem::path root_;
    Builder builder_;
};

std::string normalize_output(const std::string& output);
std::string format_duration(std::chrono::milliseconds duration);
ProblemLimits load_problem_limits(const Problem& problem);

} // namespace cfx
