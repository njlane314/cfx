#include "assets.hpp"

#include <cstdlib>
#include <stdexcept>
#include <system_error>

namespace cfx {

std::filesystem::path asset_root(const std::filesystem::path& fallback) {
    const char* configured = std::getenv("CFX_ASSET_ROOT");
    const std::filesystem::path selected =
        configured != nullptr && *configured != '\0' ? configured : fallback;

    std::error_code error;
    const auto absolute = std::filesystem::absolute(selected, error);
    if (error) {
        throw std::runtime_error("cannot resolve cfx asset root '" + selected.string() +
                                 "': " + error.message());
    }
    const auto canonical = std::filesystem::weakly_canonical(absolute, error);
    return error ? absolute.lexically_normal() : canonical;
}

} // namespace cfx
