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
constexpr bool MULTI = true;

#ifdef LOCAL
#include "debug.hpp"
#define dbg(...) debug(#__VA_ARGS__ __VA_OPT__(,) __VA_ARGS__)
#else
#define dbg(...) ((void)0)
#endif

void solve() {
    int n;
    cin >> n;

    vector<int> a(2 * n);
    int z1 = -1;
    int z2 = -1;
    for (int i = 0; i < 2 * n; i++) {
        cin >> a[i];
        if (a[i] == 0) {
            if (z1 == -1) {
                z1 = i;
            } else {
                z2 = i;
            }
        }
    }

    auto expand_mex = [&](int l, int r) {
        vector<char> seen(n + 1);
        int ans = 0;

        while (0 <= l && r < 2 * n && a[l] == a[r]) {
            seen[a[l]] = true;
            while (seen[ans]) {
                ans++;
            }
            l--;
            r++;
        }

        return ans;
    };

    // the best palindrome must contain 0, so only these three centers matter
    cout << max({
        expand_mex(z1, z1),
        expand_mex(z2, z2),
        expand_mex((z1 + z2) / 2, (z1 + z2 + 1) / 2),
    }) << '\n';
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
