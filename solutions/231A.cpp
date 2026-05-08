// CF ____: ____. O(__).

#include <bits/stdc++.h>
using namespace std;

#include "lib/core.hpp"
#include "lib/debug.hpp"

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
