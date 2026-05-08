#include "cp.hpp"

void solve() {
    int n;
    scan(n);

    for (int i = 0; i < n; i++) {
        string word;
        scan(word);

        if (word.size() <= 10) {
            println(word);
        } else {
            println(word.front() + to_string(word.size() - 2) + word.back());
        }
    }
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    solve();
    return 0;
}
