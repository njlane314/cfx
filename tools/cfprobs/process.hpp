#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cfprobs {

struct ProcessResult {
    int status = 0;
    bool timed_out = false;
    std::chrono::milliseconds elapsed{0};
};

struct ProcessOptions {
    std::optional<std::filesystem::path> stdin_path;
    std::optional<std::filesystem::path> stdout_path;
    std::optional<std::filesystem::path> stderr_path;
    std::optional<std::chrono::milliseconds> timeout;
    std::optional<std::filesystem::path> working_directory;
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

} // namespace cfprobs
