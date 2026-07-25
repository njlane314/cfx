#include <iostream>

#include "cp/prelude.hpp"

#ifdef LOCAL
#include "cp/debug.hpp"
#define dbg(...) ::cp::debug(#__VA_ARGS__ __VA_OPT__(, ) __VA_ARGS__)
#else
#define dbg(...) ((void)0)
#endif

namespace {

constexpr bool multiple_test_cases = false;

void solve() {}

} // namespace

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    int test_cases = 1;
    if constexpr (multiple_test_cases) {
        std::cin >> test_cases;
    }
    while (test_cases-- > 0) {
        solve();
    }
}
