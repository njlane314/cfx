#pragma once

#include <cstdint>

// Small, problem-agnostic conveniences. Keep this header deliberately narrow;
// data structures and algorithms belong in their own headers.
namespace cp {

using i64 = std::int64_t;
using u64 = std::uint64_t;

template <class T> constexpr bool chmin(T& value, const T& candidate) {
    if (candidate < value) {
        value = candidate;
        return true;
    }
    return false;
}

template <class T> constexpr bool chmax(T& value, const T& candidate) {
    if (value < candidate) {
        value = candidate;
        return true;
    }
    return false;
}

} // namespace cp
