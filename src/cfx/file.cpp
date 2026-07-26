#include "file.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace cfx {

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output) {
        throw std::runtime_error("cannot write " + path.string());
    }
}

void write_atomic(const std::filesystem::path& path, std::string_view contents) {
    if (std::filesystem::is_regular_file(path) && read_text(path) == contents) {
        return;
    }

    const std::filesystem::path temporary = path.string() + ".tmp." + std::to_string(::getpid());
    try {
        write_text(temporary, contents);
        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        if (error) {
            throw std::runtime_error("cannot replace " + path.string() + ": " + error.message());
        }
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

} // namespace cfx
