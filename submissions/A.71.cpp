#include <bits/stdc++.h>

using namespace std;

using ll = long long;

void solve(istream& in = cin, ostream& out = cout) {
    int n;
    in >> n;

    for (int i = 0; i < n; i++) {
        string word;
        in >> word;

        if (word.size() <= 10) {
            out << word << '\n';
        } else {
            out << word.front() + to_string(word.size() - 2) + word.back() << '\n';
        }
    }
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    solve();
    return 0;
}
