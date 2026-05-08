// CF 4A: Watermelon. O(1).
// solution is to check if w is even and greater than 2

#include <bits/stdc++.h>
using namespace std;


// ===== BEGIN lib/core.hpp =====

namespace cp {
}  // namespace cp
// ===== END  =====


// ===== BEGIN lib/debug.hpp =====

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
// ===== END  =====


using namespace cp;

using ll = long long;

static constexpr bool MULTI_TEST = false;

void solve_case(istream& in, ostream& out, int tc) {
    (void)in;
    (void)out;
    (void)tc;

    int w;
    in >> w;

    out << (w > 2 && w % 2 == 0 ? "YES" : "NO") << '\n';
}

void solve(istream& in = cin, ostream& out = cout) {
    int T = 1;
    if constexpr (MULTI_TEST) {
        in >> T;
    }

    for (int tc = 1; tc <= T; ++tc) {
        solve_case(in, out, tc);
    }
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    solve();
    return 0;
}
