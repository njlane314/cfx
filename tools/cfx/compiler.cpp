#include "compiler.hpp"

#include "bundle.hpp"
#include "hash.hpp"
#include "problem.hpp"
#include "process.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

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

std::string sanitize(std::string value) {
    for (char& character : value) {
        const bool safe =
            (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' || character == '_';
        if (!safe) {
            character = '_';
        }
    }
    return value.empty() ? "source" : value;
}

std::vector<std::string> compile_flags(bool checked, bool local) {
    std::vector<std::string> flags{
        "-std=" + configured_standard(),
        "-pipe",
        "-Wall",
        "-Wextra",
        "-Wshadow",
        "-Wformat=2",
        "-pthread",
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
                             const std::vector<std::string>& flags,
                             const std::filesystem::path& include_root) {
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
    append(version.output);
    for (const std::string& flag : flags) {
        append(flag);
    }
    for (const char* name : {"SDKROOT", "CPATH", "CPLUS_INCLUDE_PATH", "LIBRARY_PATH"}) {
        append(std::string(name) + "=" + environment(name));
    }
    std::vector<std::filesystem::path> local_headers;
    if (std::filesystem::is_directory(include_root)) {
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::recursive_directory_iterator(include_root)) {
            if (entry.is_regular_file()) {
                local_headers.push_back(entry.path());
            }
        }
    }
    std::sort(local_headers.begin(), local_headers.end());
    for (const std::filesystem::path& header : local_headers) {
        append(std::filesystem::relative(header, include_root).generic_string());
        std::ifstream input(header, std::ios::binary);
        if (!input) {
            throw std::runtime_error("cannot fingerprint local header: " + header.string());
        }
        const std::string contents{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>(),
        };
        append(contents);
    }
    return fingerprint;
}

void write_if_different(const std::filesystem::path& path, const std::string& content) {
    if (std::filesystem::is_regular_file(path)) {
        std::ifstream input(path, std::ios::binary);
        const std::string current{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>(),
        };
        if (current == content) {
            return;
        }
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output || !(output << content)) {
        throw std::runtime_error("cannot write " + path.string());
    }
}

} // namespace

Builder::Builder(std::filesystem::path root)
    : root_(std::filesystem::weakly_canonical(std::move(root))) {}

std::string Builder::bundled_source(const std::filesystem::path& source) const {
    return bundle(source, root_);
}

BuildResult Builder::build_problem(const Problem& problem, const BuildOptions& options) const {
    return build_source(problem.solution_path(), problem.id(), options);
}

BuildResult Builder::build_source(const std::filesystem::path& source,
                                  const std::string& cache_name,
                                  const BuildOptions& options) const {
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(source);
    if (!std::filesystem::is_regular_file(canonical)) {
        throw std::runtime_error("source not found: " + source.string());
    }

    const std::string prepared = bundled_source(canonical);
    const std::vector<std::string> compiler = compiler_command();
    const std::vector<std::string> flags = compile_flags(options.checked, options.local);
    const std::string digest =
        content_digest(join_fingerprint(prepared, compiler, flags, root_ / "include"));

    const std::filesystem::path cache = root_ / ".build" / "cache" / sanitize(cache_name);
    const std::filesystem::path sources = cache / "sources";
    const std::filesystem::path binaries = cache / "bin";
    std::filesystem::create_directories(sources);
    std::filesystem::create_directories(binaries);

    const std::filesystem::path bundled = sources / (digest + ".cpp");
    const std::filesystem::path binary = binaries / digest;
    write_if_different(bundled, prepared);

    bool compiled = false;
    if (options.rebuild || !std::filesystem::is_regular_file(binary)) {
        const std::filesystem::path temporary =
            binaries / ("." + digest + "." + std::to_string(::getpid()) + ".tmp");
        std::vector<std::string> command = compiler;
        command.insert(command.end(), flags.begin(), flags.end());
        command.push_back("-I");
        command.push_back((root_ / "include").string());
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
        compiled = true;
    }

    return BuildResult{
        binary,
        bundled,
        digest,
        compiled,
        std::filesystem::file_size(canonical),
        std::filesystem::file_size(binary),
    };
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
    return environment("STD", "gnu++20");
}

} // namespace cfx
