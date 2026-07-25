#pragma once

#include <cassert>
#include <cstddef>
#include <vector>

// Zero-based Fenwick tree for additive values. add, prefix_sum and range_sum
// are O(log n). prefix_sum(end) covers [0, end), and range_sum covers [l, r).
// T must support default zero, += and subtraction; its overflow rules apply.
namespace cp {

template <class T> class fenwick_tree {
  public:
    explicit fenwick_tree(std::size_t size) : tree_(size + 1, T{}) {}

    [[nodiscard]] std::size_t size() const {
        return tree_.size() - 1;
    }

    void add(std::size_t index, const T& delta) {
        assert(index < size());
        for (++index; index < tree_.size(); index += index & -index) {
            tree_[index] += delta;
        }
    }

    [[nodiscard]] T prefix_sum(std::size_t end) const {
        assert(end <= size());
        T result{};
        for (; end > 0; end -= end & -end) {
            result += tree_[end];
        }
        return result;
    }

    [[nodiscard]] T range_sum(std::size_t left, std::size_t right) const {
        assert(left <= right && right <= size());
        return prefix_sum(right) - prefix_sum(left);
    }

  private:
    std::vector<T> tree_;
};

} // namespace cp
