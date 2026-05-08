// CF ____: ____. O(__).

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

void solve(istream& in = cin, ostream& out = cout) {
    int n; 
    in >> n; 

    int k = 0;
    for (int i = 0; i < n; i++) {
        int a, b, c; 
        in >> a >> b >> c; 
        if (a + b + c > 1) 
            k += 1;
    }

    out << k << '\n';   
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    solve();
    return 0;
}
