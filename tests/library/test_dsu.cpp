#include "cp/ds/dsu.hpp"

#include <algorithm>
#include <cassert>
#include <numeric>
#include <random>
#include <vector>

int main() {
    cp::disjoint_set_union empty(0);
    assert(empty.size() == 0);
    assert(empty.components() == 0);

    constexpr int size = 41;
    cp::disjoint_set_union sets(size);
    std::vector<int> label(size);
    std::iota(label.begin(), label.end(), 0);

    std::mt19937 random(0xD515EA5EU);
    for (int step = 0; step < 5'000; ++step) {
        const int first = static_cast<int>(random() % size);
        const int second = static_cast<int>(random() % size);
        const bool were_same = label[first] == label[second];

        if (step % 3 != 0) {
            assert(sets.unite(first, second) == !were_same);
            if (!were_same) {
                const int replaced = label[second];
                const int replacement = label[first];
                std::replace(label.begin(), label.end(), replaced, replacement);
            }
        } else {
            assert(sets.same(first, second) == were_same);
        }

        const int vertex = static_cast<int>(random() % size);
        const int expected_size =
            static_cast<int>(std::count(label.begin(), label.end(), label[vertex]));
        assert(sets.component_size(vertex) == expected_size);

        std::vector<int> distinct = label;
        std::sort(distinct.begin(), distinct.end());
        distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
        assert(sets.components() == static_cast<int>(distinct.size()));
    }
}
