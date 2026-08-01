#pragma once

#include "problem.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cfx {

struct Sample {
    std::string input;
    std::string output;
};

struct CompanionPackage {
    Problem problem;
    std::string url;
    std::string name;
    int time_limit_ms = 0;
    int memory_limit_mb = 0;
    std::vector<Sample> samples;
};

struct ImportResult {
    Problem problem;
    std::size_t sample_count = 0;
    std::size_t files_written = 0;
};

CompanionPackage parse_companion_package(std::string_view payload,
                                         const std::filesystem::path& root);

ImportResult import_companion_package(const CompanionPackage& package,
                                      const std::filesystem::path& root, bool force = false);

} // namespace cfx
