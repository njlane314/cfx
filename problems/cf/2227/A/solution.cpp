#include <iostream>

void solve() {
    int pairs;
    std::cin >> pairs;

    while (pairs-- > 0) {
        int a;
        int b;
        std::cin >> a >> b;
        std::cout << (a % 2 == 0 || b % 2 == 0 ? "YES" : "NO") << '\n';
    }
}

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    solve();
}
