#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cfx {

struct ProcessResult {
    int status = 0;
    int exit_code = 0;
    int signal = 0;
    bool timed_out = false;
    bool memory_limit_exceeded = false;
    bool output_limit_exceeded = false;
    std::chrono::milliseconds elapsed{0};
    std::chrono::milliseconds cpu_time{0};
    std::uint64_t peak_memory_bytes = 0;
};

struct ProcessOptions {
    std::optional<std::filesystem::path> stdin_path = std::nullopt;
    std::optional<std::filesystem::path> stdout_path = std::nullopt;
    std::optional<std::filesystem::path> stderr_path = std::nullopt;
    std::optional<std::chrono::milliseconds> timeout = std::nullopt;
    std::optional<std::filesystem::path> working_directory = std::nullopt;
    std::optional<std::uint64_t> memory_limit_bytes = std::nullopt;
    std::optional<std::uint64_t> output_limit_bytes = std::nullopt;
};

ProcessResult run_process(const std::vector<std::string>& arguments,
                          const ProcessOptions& options = {});

// Start a process in a detached session and return once its executable has
// loaded. Standard streams are connected to /dev/null.
void launch_detached_process(const std::vector<std::string>& arguments);

struct CaptureResult {
    int status = 0;
    std::string output;
};

CaptureResult capture_process(const std::vector<std::string>& arguments);

std::vector<std::string> split_command_words(const std::string& value);

} // namespace cfx
