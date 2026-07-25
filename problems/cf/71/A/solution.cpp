#include <iostream>
#include <string>

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    int words;
    std::cin >> words;
    while (words-- > 0) {
        std::string word;
        std::cin >> word;
        if (word.size() <= 10) {
            std::cout << word << '\n';
        } else {
            std::cout << word.front() << word.size() - 2 << word.back() << '\n';
        }
    }
}
