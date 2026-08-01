#include "companion.hpp"

#include "file.hpp"
#include "json.hpp"
#include "workspace.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace cfx {
namespace {

namespace fs = std::filesystem;

std::string with_final_newline(std::string value) {
    if (value.empty() || value.back() != '\n') {
        value.push_back('\n');
    }
    return value;
}

int integer_field(const Json& object, std::string_view name) {
    const Json* value = object.find(name);
    if (value == nullptr) {
        return 0;
    }
    if (!value->is_number() || value->number() < 0.0 ||
        value->number() > static_cast<double>(std::numeric_limits<int>::max()) ||
        std::trunc(value->number()) != value->number()) {
        throw std::runtime_error("problem package field '" + std::string(name) +
                                 "' must be a non-negative integer");
    }
    return static_cast<int>(value->number());
}

std::string string_field(const Json& object, std::string_view name) {
    const Json* value = object.find(name);
    return value != nullptr && value->is_string() ? value->string() : std::string{};
}

std::string sample_name(std::size_t number, const char* extension) {
    std::ostringstream output;
    output << std::setw(2) << std::setfill('0') << number << extension;
    return output.str();
}

void recover_sample_transaction(const Problem& problem) {
    const fs::path directory = problem.state_directory();
    if (!fs::is_directory(directory)) {
        return;
    }
    const fs::path samples = problem.samples_path();
    const fs::path backup = directory / ".samples.backup";
    std::error_code error;
    if (fs::is_directory(backup)) {
        if (!fs::exists(samples)) {
            fs::rename(backup, samples, error);
            if (error) {
                throw std::runtime_error("cannot recover fetched samples: " + error.message());
            }
        } else {
            fs::remove_all(backup, error);
        }
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
        const std::string name = entry.path().filename().string();
        if (entry.is_directory() && name.starts_with(".samples.stage.")) {
            fs::remove_all(entry.path(), error);
        }
    }
}

Problem problem_from_url(std::string_view url, const fs::path& root) {
    constexpr std::string_view origin = "https://codeforces.com/";
    if (!url.starts_with(origin)) {
        throw std::runtime_error("problem package has an invalid URL");
    }
    url.remove_prefix(origin.size());
    if (url.ends_with('/')) url.remove_suffix(1);

    std::string_view contest;
    std::string index;
    if (url.starts_with("contest/")) {
        url.remove_prefix(8);
        const std::size_t split = url.find("/problem/");
        contest = url.substr(0, split);
        index = split == std::string_view::npos ? "" : std::string(url.substr(split + 9));
    } else if (url.starts_with("problemset/problem/")) {
        url.remove_prefix(19);
        const std::size_t split = url.find('/');
        contest = url.substr(0, split);
        index = split == std::string_view::npos ? "" : std::string(url.substr(split + 1));
    }
    std::transform(index.begin(), index.end(), index.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    try {
        return Problem(std::string(contest), std::move(index), root);
    } catch (const ProblemError&) {
        throw std::runtime_error("problem package has an invalid URL");
    }
}

} // namespace

CompanionPackage parse_companion_package(std::string_view payload, const fs::path& root) {
    const Json document = parse_json(payload);
    const std::string url = string_field(document, "url");
    if (url.empty()) {
        throw std::runtime_error("problem package has no URL");
    }
    CompanionPackage package{
        problem_from_url(url, root),
        url,
        string_field(document, "name"),
        integer_field(document, "timeLimit"),
        integer_field(document, "memoryLimit"),
        {},
    };
    const Json* tests = document.find("tests");
    if (tests == nullptr || !tests->is_array()) {
        throw std::runtime_error("problem package has no tests");
    }
    for (const Json& test : tests->array()) {
        package.samples.push_back(Sample{
            string_field(test, "input"),
            string_field(test, "output"),
        });
    }
    if (package.samples.empty()) {
        throw std::runtime_error("problem package has no tests");
    }
    return package;
}

void import_companion_package(const CompanionPackage& package, const fs::path& root) {
    recover_sample_transaction(package.problem);
    Workspace(root).create(package.problem);
    const fs::path samples = package.problem.samples_path();

    struct Expected {
        std::string input_name;
        std::string output_name;
        std::string input;
        std::string output;
    };
    std::vector<Expected> expected;
    for (std::size_t index = 0; index < package.samples.size(); ++index) {
        expected.push_back(Expected{
            sample_name(index + 1, ".in"),
            sample_name(index + 1, ".out"),
            with_final_newline(package.samples[index].input),
            with_final_newline(package.samples[index].output),
        });
    }

    bool identical = true;
    for (const fs::directory_entry& entry : fs::directory_iterator(samples)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() == ".in" || entry.path().extension() == ".out") {
            const std::string name = entry.path().filename().string();
            const bool known =
                std::any_of(expected.begin(), expected.end(), [&](const Expected& value) {
                    return value.input_name == name || value.output_name == name;
                });
            if (!known) {
                identical = false;
            }
        }
    }
    for (const Expected& pair : expected) {
        const fs::path input = samples / pair.input_name;
        const fs::path output = samples / pair.output_name;
        if (!fs::is_regular_file(input) || !fs::is_regular_file(output) ||
            read_text(input) != pair.input || read_text(output) != pair.output) {
            identical = false;
        }
    }

    if (!identical) {
        const fs::path state = package.problem.state_directory();
        const fs::path stage = state / (".samples.stage." + std::to_string(::getpid()));
        const fs::path backup = state / ".samples.backup";
        std::error_code ignored;
        fs::remove_all(stage, ignored);
        fs::remove_all(backup, ignored);
        fs::create_directories(stage);
        try {
            for (const Expected& pair : expected) {
                write_text(stage / pair.input_name, pair.input);
                write_text(stage / pair.output_name, pair.output);
            }
            fs::rename(samples, backup);
            try {
                fs::rename(stage, samples);
            } catch (...) {
                fs::rename(backup, samples);
                throw;
            }
            fs::remove_all(backup, ignored);
        } catch (...) {
            fs::remove_all(stage, ignored);
            if (!fs::exists(samples) && fs::exists(backup)) {
                std::error_code restore_error;
                fs::rename(backup, samples, restore_error);
            }
            throw;
        }
    }

    const fs::path metadata = package.problem.metadata_path();
    write_atomic(metadata, "{\n"
                           "  \"id\": " +
                               json_quote(package.problem.id()) +
                               ",\n"
                               "  \"name\": " +
                               json_quote(package.name) +
                               ",\n"
                               "  \"url\": " +
                               json_quote(package.url) +
                               ",\n"
                               "  \"timeLimitMs\": " +
                               std::to_string(package.time_limit_ms) +
                               ",\n"
                               "  \"memoryLimitMb\": " +
                               std::to_string(package.memory_limit_mb) +
                               "\n"
                               "}\n");
}

} // namespace cfx
