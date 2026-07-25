#include <iostream>
#include <vector>

using Integer = long long;

void solve() {
    int count;
    std::cin >> count;

    std::vector<Integer> two;
    std::vector<Integer> neutral;
    std::vector<Integer> three;
    std::vector<Integer> six;
    while (count-- > 0) {
        Integer value;
        std::cin >> value;
        if (value % 6 == 0) {
            six.push_back(value);
        } else if (value % 2 == 0) {
            two.push_back(value);
        } else if (value % 3 == 0) {
            three.push_back(value);
        } else {
            neutral.push_back(value);
        }
    }

    const auto print = [](const std::vector<Integer>& values) {
        for (const Integer value : values) {
            std::cout << value << ' ';
        }
    };
    print(two);
    print(neutral);
    print(three);
    print(six);
    std::cout << '\n';
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
