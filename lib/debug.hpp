#pragma once

#ifdef LOCAL
#include <iostream>

namespace cp::debug_detail {
inline void print_many() {}

template <class T, class... Rest>
void print_many(const T& value, const Rest&... rest) {
    std::cerr << value;
    ((std::cerr << ' ' << rest), ...);
}
}  // namespace cp::debug_detail

#define dbg(...)                                      \
    do {                                             \
        std::cerr << "[" << #__VA_ARGS__ << "] = "; \
        ::cp::debug_detail::print_many(__VA_ARGS__); \
        std::cerr << '\n';                           \
    } while (false)
#else
#define dbg(...) ((void)0)
#endif
