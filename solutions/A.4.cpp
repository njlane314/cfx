#include "cp.hpp"

void solve() {
    int w;
    scan(w);
    yesno(w > 2 && w % 2 == 0);
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    solve();
    return 0;
}
