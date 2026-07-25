#include <iostream>
#include <string>

void solve() {
    int length;
    std::string brackets;
    std::cin >> length >> brackets;

    int open = 0;
    for (const char bracket : brackets) {
        open += bracket == '(';
    }
    std::cout << (2 * open == length ? "YES" : "NO") << '\n';
}

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    int tests;
    std::cin >> tests;
    while (tests-- > 0) {
        solve();
    }
}
