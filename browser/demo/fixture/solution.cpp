#include <iostream>
#include <string>

void solve() {
    std::string word;
    std::cin >> word;
    if (word.size() <= 10)
        std::cout << word;
    else
        std::cout << word.front() << word.size() - 2 << word.back();
}

int main() {
    int count = 0;
    std::cin >> count;
    while (count-- > 0) {
        solve();
        std::cout << '\n';
    }
}
