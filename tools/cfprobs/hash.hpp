#pragma once

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace cfprobs {

// A deterministic, compact cache key. This is not used for security.
inline std::string content_digest(std::string_view value) {
    std::uint64_t first = 14695981039346656037ULL;
    std::uint64_t second = 1099511628211ULL;
    for (const unsigned char byte : value) {
        first ^= byte;
        first *= 1099511628211ULL;

        second ^= static_cast<std::uint64_t>(byte) + 0x9e3779b97f4a7c15ULL;
        second *= 14029467366897019727ULL;
        second ^= second >> 31U;
    }

    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << first << std::setw(16) << second;
    return stream.str();
}

} // namespace cfprobs
