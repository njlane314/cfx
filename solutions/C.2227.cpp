#include <bits/stdc++.h>

using namespace std;

using ll = long long;
using ull = unsigned long long;
using ld = long double;

using pii = pair<int, int>;
using pll = pair<ll, ll>;

#define all(x) begin(x), end(x)
#define rall(x) rbegin(x), rend(x)

void solve() {
    int t;
    cin >> t;

    while (t--) {
        int n;
        cin >> n;

        // only the factors needed for 6 matter
        vector<ll> two;
        vector<ll> neutral;
        vector<ll> three;
        vector<ll> six;

        for (int i = 0; i < n; i++) {
            ll x;
            cin >> x;

            if (x % 6 == 0) {
                six.push_back(x);
            } else if (x % 2 == 0) {
                two.push_back(x);
            } else if (x % 3 == 0) {
                three.push_back(x);
            } else {
                neutral.push_back(x);
            }
        }

        auto print = [](const vector<ll>& v) {
            for (ll x : v) {
                cout << x << ' ';
            }
        };

        // keep 2-only and 3-only apart; neutrals do not join them
        print(two);
        print(neutral);
        print(three);
        print(six);
        cout << '\n';
    }
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    solve();
    return 0;
}
