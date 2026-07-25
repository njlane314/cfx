#include <iostream>

void solve() {
    int problems;
    std::cin >> problems;

    int solved = 0;
    while (problems-- > 0) {
        int first;
        int second;
        int third;
        std::cin >> first >> second >> third;
        solved += first + second + third >= 2;
    }

    std::cout << solved << '\n';
}

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    solve();
}
