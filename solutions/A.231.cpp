#include "cp.hpp"

void solve() {
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

    solve();
    return 0;
}
