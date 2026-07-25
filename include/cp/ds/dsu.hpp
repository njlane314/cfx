#pragma once

#include <cassert>
#include <cstddef>
#include <vector>

// Zero-based disjoint-set union with path compression and union by size.
// find/unite are amortized O(alpha(n)); component queries require valid indices.
// A root stores the negative component size, all other entries store a parent.
// The number of vertices and every component size must fit in int.
namespace cp {

class disjoint_set_union {
  public:
    explicit disjoint_set_union(int size) {
        assert(size >= 0);
        parent_or_size_.assign(static_cast<std::size_t>(size), -1);
        components_ = size;
    }

    [[nodiscard]] int size() const {
        return static_cast<int>(parent_or_size_.size());
    }

    [[nodiscard]] int components() const {
        return components_;
    }

    int find(int vertex) {
        assert(0 <= vertex && vertex < size());

        int root = vertex;
        while (parent_or_size_[root] >= 0) {
            root = parent_or_size_[root];
        }
        while (vertex != root) {
            const int parent = parent_or_size_[vertex];
            parent_or_size_[vertex] = root;
            vertex = parent;
        }
        return root;
    }

    bool unite(int first, int second) {
        first = find(first);
        second = find(second);
        if (first == second) {
            return false;
        }

        if (-parent_or_size_[first] < -parent_or_size_[second]) {
            const int temporary = first;
            first = second;
            second = temporary;
        }
        parent_or_size_[first] += parent_or_size_[second];
        parent_or_size_[second] = first;
        --components_;
        return true;
    }

    bool same(int first, int second) {
        return find(first) == find(second);
    }

    int component_size(int vertex) {
        return -parent_or_size_[find(vertex)];
    }

  private:
    std::vector<int> parent_or_size_;
    int components_ = 0;
};

} // namespace cp
