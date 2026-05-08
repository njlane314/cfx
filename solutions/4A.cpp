// CF 4A: Watermelon. O(1).
// solution is to check if w is even and greater than 2

#include <bits/stdc++.h>
using namespace std;

#include "lib/core.hpp"
#include "lib/debug.hpp"

using namespace cp;

using ll = long long;

void solve(istream& in = cin, ostream& out = cout) {
    int w;
    in >> w;

    out << (w > 2 && w % 2 == 0 ? "YES" : "NO") << '\n';
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    solve();
    return 0;
}
