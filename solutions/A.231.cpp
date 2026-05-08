#include <bits/stdc++.h>

using namespace std;

using ll = long long;
using ull = unsigned long long;
using ld = long double;
using pii = pair<int, int>;
using pll = pair<ll, ll>;

#define all(x) begin(x), end(x)
#define rall(x) rbegin(x), rend(x)

template <class T>
using min_heap = priority_queue<T, vector<T>, greater<T>>;

template <class T>
bool chmin(T& a, const T& b) {
    if (b < a) {
        a = b;
        return true;
    }
    return false;
}

template <class T>
bool chmax(T& a, const T& b) {
    if (a < b) {
        a = b;
        return true;
    }
    return false;
}

template <class C>
int sz(const C& c) {
    return static_cast<int>(c.size());
}

[[maybe_unused]] constexpr int INF = numeric_limits<int>::max() / 2;
[[maybe_unused]] constexpr ll LINF = numeric_limits<ll>::max() / 4;
constexpr bool MULTI = false;

#ifdef LOCAL
#include "debug.hpp"
#define dbg(...) debug(#__VA_ARGS__ __VA_OPT__(,) __VA_ARGS__)
#else
#define dbg(...) ((void)0)
#endif

void solve() {
    int n;
    cin >> n;

    int k = 0;
    for (int i = 0; i < n; i++) {
        int a, b, c;
        cin >> a >> b >> c;

        // solve it when at least two are sure
        if (a + b + c > 1) {
            k += 1;
        }
    }

    cout << k << '\n';
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    int tc = 1;
    if constexpr (MULTI) {
        cin >> tc;
    }
    while (tc--) {
        solve();
    }

    return 0;
}
