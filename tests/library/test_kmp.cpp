#include "cp/string/kmp.hpp"

#include <cassert>
#include <cstddef>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::size_t> brute_matches(std::string_view text, std::string_view pattern) {
    std::vector<std::size_t> matches;
    for (std::size_t index = 0; index + pattern.size() <= text.size(); ++index) {
        if (text.substr(index, pattern.size()) == pattern) {
            matches.push_back(index);
        }
    }
    return matches;
}

std::string random_string(std::mt19937& random, std::size_t length) {
    std::string result(length, 'a');
    for (char& character : result) {
        character = static_cast<char>('a' + random() % 3);
    }
    return result;
}

} // namespace

int main() {
    const cp::kmp_matcher matcher("ababcabab");
    assert(matcher.prefix_table() == std::vector<std::size_t>({0, 0, 1, 2, 0, 1, 2, 3, 4}));

    const cp::kmp_matcher overlapping("aaa");
    assert(overlapping.find_all("aaaaa") == std::vector<std::size_t>({0, 1, 2}));
    assert(cp::kmp_matcher("").find_all("abc") == std::vector<std::size_t>({0, 1, 2, 3}));

    std::mt19937 random(0x4B4D50U);
    for (int step = 0; step < 10'000; ++step) {
        const std::string text = random_string(random, random() % 31);
        const std::string pattern = random_string(random, random() % 11);
        assert(cp::kmp_matcher(pattern).find_all(text) == brute_matches(text, pattern));
    }
}
