#include "compiler.hpp"

#include "bundle.hpp"
#include "file.hpp"
#include "hash.hpp"
#include "problem.hpp"
#include "process.hpp"
#include "runtime.hpp"

#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace cfx {
namespace {

std::string environment(const char* name, const std::string& fallback = {}) {
    const char* value = std::getenv(name);
    return value == nullptr ? fallback : std::string(value);
}

std::vector<std::string> compiler_command() {
    std::vector<std::string> command = split_command_words(environment("CXX", "c++"));
    if (command.empty()) {
        throw std::runtime_error("CXX names no compiler");
    }
    return command;
}

std::vector<std::string> compile_flags(const std::string& standard, bool checked, bool local) {
    std::vector<std::string> flags{
        "-std=" + standard, "-pipe", "-Wall", "-Wextra", "-Wshadow", "-Wformat=2", "-pthread",
    };
    if (local) {
        flags.push_back("-DLOCAL");
    }
    if (checked) {
        const std::vector<std::string> diagnostic{
            "-O1",
            "-g",
            "-pedantic",
            "-Wconversion",
            "-Wsign-conversion",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-fno-sanitize-recover=all",
        };
        flags.insert(flags.end(), diagnostic.begin(), diagnostic.end());
    } else {
        flags.push_back("-O2");
    }

    const std::string custom = environment("CFX_CXXFLAGS");
    if (!custom.empty()) {
        std::vector<std::string> additions = split_command_words(custom);
        flags.insert(flags.end(), additions.begin(), additions.end());
    }
    if (!local) {
        flags.push_back("-ULOCAL");
        flags.push_back("-DONLINE_JUDGE");
    }
    return flags;
}

std::string join_fingerprint(const std::string& source, const std::vector<std::string>& compiler,
                             const std::vector<std::string>& flags) {
    std::string fingerprint = source;
    const auto append = [&](const std::string& value) {
        fingerprint.push_back('\0');
        fingerprint.append(value);
    };
    for (const std::string& part : compiler) {
        append(part);
    }
    std::vector<std::string> version_command = compiler;
    version_command.push_back("--version");
    const CaptureResult version = capture_process(version_command);
    append(std::to_string(version.status));
    append(version.output);
    for (const std::string& flag : flags) {
        append(flag);
    }
    return fingerprint;
}

} // namespace

Builder::Builder(std::filesystem::path root)
    : root_(std::filesystem::weakly_canonical(std::move(root))) {}

std::string Builder::bundled_source(const std::filesystem::path& source) const {
    return bundle(source, root_);
}

BuildResult Builder::build_problem(const Problem& problem, const BuildOptions& options) const {
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(problem.solution_path());
    if (!std::filesystem::is_regular_file(canonical)) {
        throw std::runtime_error("source not found: " + problem.solution_path().string());
    }

    const std::string prepared = bundled_source(canonical);
    const std::vector<std::string> compiler = compiler_command();
    const std::string standard = configured_standard();
    const std::vector<std::string> flags = compile_flags(standard, options.checked, options.local);
    const std::string digest = content_digest(join_fingerprint(prepared, compiler, flags));

    const std::filesystem::path cache = cfx::state_root(root_) / "build" / problem.id();
    std::filesystem::create_directories(cache);

    const std::filesystem::path bundled = cache / (digest + ".cpp");
    const std::filesystem::path binary = cache / digest;
    write_atomic(bundled, prepared);

    if (!std::filesystem::is_regular_file(binary)) {
        const std::filesystem::path temporary =
            cache / ("." + digest + "." + std::to_string(::getpid()) + ".tmp");
        std::vector<std::string> command = compiler;
        command.insert(command.end(), flags.begin(), flags.end());
        command.push_back(bundled.string());
        command.push_back("-o");
        command.push_back(temporary.string());

        const ProcessResult result = run_process(command);
        if (result.status != 0) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw std::runtime_error("compile failed: " + canonical.string());
        }
        std::error_code error;
        std::filesystem::rename(temporary, binary, error);
        if (error) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw std::runtime_error("cannot install cached binary: " + error.message());
        }
    }

    return BuildResult{binary, bundled};
}

std::string format_bytes(std::uintmax_t bytes) {
    std::ostringstream stream;
    if (bytes < 1024) {
        stream << bytes << 'B';
    } else if (bytes < 1024 * 1024) {
        stream << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / 1024.0
               << "KiB";
    } else {
        stream << std::fixed << std::setprecision(1)
               << static_cast<double>(bytes) / (1024.0 * 1024.0) << "MiB";
    }
    return stream.str();
}

std::string configured_standard() {
    return environment("CFX_STD", "c++20");
}

} // namespace cfx
