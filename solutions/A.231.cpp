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

namespace io {
template <class T>
void read(T& x) {
    cin >> x;
}

template <class A, class B>
void read(pair<A, B>& p) {
    read(p.first);
    read(p.second);
}

template <class T>
void read(vector<T>& v) {
    for (auto& x : v) {
        read(x);
    }
}

template <class T, size_t N>
void read(array<T, N>& a) {
    for (auto& x : a) {
        read(x);
    }
}

template <class... Ts>
void scan(Ts&... xs) {
    (read(xs), ...);
}

template <class T>
T input() {
    T x;
    read(x);
    return x;
}

template <class T>
vector<T> vec(int n, int base = 0) {
    vector<T> v(n + base);
    for (int i = base; i < n + base; i++) {
        read(v[i]);
    }
    return v;
}

template <class T>
vector<vector<T>> matrix(int n, int m, int rbase = 0, int cbase = 0) {
    vector<vector<T>> a(n + rbase, vector<T>(m + cbase));
    for (int i = rbase; i < n + rbase; i++) {
        for (int j = cbase; j < m + cbase; j++) {
            read(a[i][j]);
        }
    }
    return a;
}

vector<string> grid(int n) {
    vector<string> g(n);
    for (auto& row : g) {
        cin >> row;
    }
    return g;
}

vector<string> padded_grid(int n, int m, char pad = '#') {
    vector<string> g(n + 2, string(m + 2, pad));
    for (int i = 1; i <= n; i++) {
        string s;
        cin >> s;
        for (int j = 1; j <= m; j++) {
            g[i][j] = s[j - 1];
        }
    }
    return g;
}

vector<pair<int, int>> edges(int m, int input_base = 1, int output_base = 0) {
    vector<pair<int, int>> e;
    e.reserve(m);

    for (int i = 0; i < m; i++) {
        int u, v;
        cin >> u >> v;
        u = u - input_base + output_base;
        v = v - input_base + output_base;
        e.push_back({u, v});
    }

    return e;
}

vector<vector<int>> graph(
    int n,
    int m,
    bool directed = false,
    int input_base = 1,
    int output_base = 0
) {
    vector<vector<int>> g(n + output_base);

    for (auto [u, v] : edges(m, input_base, output_base)) {
        g[u].push_back(v);
        if (!directed) {
            g[v].push_back(u);
        }
    }

    return g;
}

template <class W>
vector<tuple<int, int, W>> weighted_edges(
    int m,
    int input_base = 1,
    int output_base = 0
) {
    vector<tuple<int, int, W>> e;
    e.reserve(m);

    for (int i = 0; i < m; i++) {
        int u, v;
        W w;
        cin >> u >> v >> w;
        u = u - input_base + output_base;
        v = v - input_base + output_base;
        e.push_back({u, v, w});
    }

    return e;
}

template <class W>
vector<vector<pair<int, W>>> weighted_graph(
    int n,
    int m,
    bool directed = false,
    int input_base = 1,
    int output_base = 0
) {
    vector<vector<pair<int, W>>> g(n + output_base);

    for (auto [u, v, w] : weighted_edges<W>(m, input_base, output_base)) {
        g[u].push_back({v, w});
        if (!directed) {
            g[v].push_back({u, w});
        }
    }

    return g;
}

template <class T>
void print_one(const T& x) {
    cout << x;
}

template <class A, class B>
void print_one(const pair<A, B>& p) {
    print_one(p.first);
    cout << ' ';
    print_one(p.second);
}

void println() {
    cout << '\n';
}

template <class T, class... Ts>
void println(const T& first, const Ts&... rest) {
    print_one(first);
    ((cout << ' ', print_one(rest)), ...);
    cout << '\n';
}

template <class T>
void print_vec(const vector<T>& v, char sep = ' ', char end = '\n') {
    for (int i = 0; i < (int)v.size(); i++) {
        if (i) {
            cout << sep;
        }
        print_one(v[i]);
    }
    cout << end;
}

void yesno(bool ok) {
    cout << (ok ? "YES\n" : "NO\n");
}

void YesNo(bool ok) {
    cout << (ok ? "Yes\n" : "No\n");
}
}  // namespace io

using namespace io;

static constexpr bool MULTI_TEST = false;

void solve_case(int tc) {
    (void)tc;

    int n;
    scan(n);

    int k = 0;
    for (int i = 0; i < n; i++) {
        int a, b, c;
        scan(a, b, c);
        if (a + b + c > 1) {
            k += 1;
        }
    }

    println(k);
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    int T = 1;
    if constexpr (MULTI_TEST) {
        cin >> T;
    }

    for (int tc = 1; tc <= T; tc++) {
        solve_case(tc);
    }

    return 0;
}
