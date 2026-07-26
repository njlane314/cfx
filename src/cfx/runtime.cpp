#include "runtime.hpp"

#include "hash.hpp"

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <system_error>

namespace cfx {
namespace {

namespace fs = std::filesystem;

fs::path normalized_absolute(const fs::path& path) {
    std::error_code error;
    const fs::path absolute = fs::absolute(path, error);
    if (error) {
        throw std::runtime_error("cannot resolve runtime path '" + path.string() +
                                 "': " + error.message());
    }
    const fs::path canonical = fs::weakly_canonical(absolute, error);
    return error ? absolute.lexically_normal() : canonical;
}

std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
}

std::string safe_name(std::string value) {
    for (char& character : value) {
        const bool safe =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' || character == '_';
        if (!safe) {
            character = '-';
        }
    }
    return value.empty() ? "archive" : value;
}

std::string archive_key(const fs::path& archive_root) {
    const fs::path canonical = normalized_absolute(archive_root);
    return safe_name(canonical.filename().string()) + "-" +
           content_digest(canonical.string()).substr(0, 12);
}

fs::path home_directory() {
    const std::string home = environment("HOME");
    if (home.empty()) {
        throw std::runtime_error("HOME is not set; configure an explicit cfx runtime root");
    }
    return normalized_absolute(home);
}

fs::path selected_root(const fs::path& archive_root) {
    const std::string explicit_root = environment("CFX_STATE_ROOT");
    if (!explicit_root.empty()) {
        return normalized_absolute(explicit_root);
    }

    const std::string xdg_root = environment("XDG_STATE_HOME");
    const fs::path base = xdg_root.empty() ? home_directory() / ".local/state/cfx"
                                           : normalized_absolute(xdg_root) / "cfx";
    return base / "workspaces" / archive_key(archive_root);
}

} // namespace

fs::path state_root(const fs::path& archive_root) {
    return selected_root(archive_root);
}

} // namespace cfx
