#include <algorithm>
#include <iostream>
#include <vector>

void solve() {
    int half_size;
    std::cin >> half_size;

    std::vector<int> values(2 * half_size);
    int first_zero = -1;
    int second_zero = -1;
    for (int index = 0; index < 2 * half_size; ++index) {
        std::cin >> values[index];
        if (values[index] == 0) {
            if (first_zero == -1) {
                first_zero = index;
            } else {
                second_zero = index;
            }
        }
    }

    const auto palindrome_mex = [&](int left, int right) {
        std::vector<char> seen(half_size + 1);
        int mex = 0;
        while (left >= 0 && right < 2 * half_size && values[left] == values[right]) {
            seen[values[left]] = true;
            while (seen[mex]) {
                ++mex;
            }
            --left;
            ++right;
        }
        return mex;
    };

    std::cout << std::max({
                     palindrome_mex(first_zero, first_zero),
                     palindrome_mex(second_zero, second_zero),
                     palindrome_mex((first_zero + second_zero) / 2,
                                    (first_zero + second_zero + 1) / 2),
                 })
              << '\n';
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
