#include "cp/ds/fenwick.hpp"

#include <cassert>
#include <cstddef>
#include <numeric>
#include <random>
#include <vector>

int main() {
    cp::fenwick_tree<long long> empty(0);
    assert(empty.size() == 0);
    assert(empty.prefix_sum(0) == 0);

    constexpr std::size_t size = 67;
    cp::fenwick_tree<long long> tree(size);
    std::vector<long long> values(size);
    std::mt19937 random(0xF311B17U);

    for (int step = 0; step < 8'000; ++step) {
        if (step % 2 == 0) {
            const std::size_t index = random() % size;
            const long long delta = static_cast<long long>(random() % 2'001) - 1'000;
            values[index] += delta;
            tree.add(index, delta);
        } else {
            std::size_t left = random() % (size + 1);
            std::size_t right = random() % (size + 1);
            if (left > right) {
                const std::size_t temporary = left;
                left = right;
                right = temporary;
            }

            const long long expected_prefix = std::accumulate(
                values.begin(), values.begin() + static_cast<std::ptrdiff_t>(right), 0LL);
            const long long expected_range =
                std::accumulate(values.begin() + static_cast<std::ptrdiff_t>(left),
                                values.begin() + static_cast<std::ptrdiff_t>(right), 0LL);
            assert(tree.prefix_sum(right) == expected_prefix);
            assert(tree.range_sum(left, right) == expected_range);
        }
    }
}
