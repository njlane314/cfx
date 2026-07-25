#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Knuth-Morris-Pratt matching over string bytes. Construction is O(pattern),
// find_all is O(text + matches), and returned positions are zero-based.
// An empty pattern matches at every boundary, including text.size().
namespace cp {

class kmp_matcher {
  public:
    explicit kmp_matcher(std::string pattern)
        : pattern_(std::move(pattern)), prefix_(pattern_.size()) {
        for (std::size_t index = 1; index < pattern_.size(); ++index) {
            std::size_t border = prefix_[index - 1];
            while (border > 0 && pattern_[index] != pattern_[border]) {
                border = prefix_[border - 1];
            }
            if (pattern_[index] == pattern_[border]) {
                ++border;
            }
            prefix_[index] = border;
        }
    }

    [[nodiscard]] std::string_view pattern() const {
        return pattern_;
    }

    [[nodiscard]] const std::vector<std::size_t>& prefix_table() const {
        return prefix_;
    }

    [[nodiscard]] std::vector<std::size_t> find_all(std::string_view text) const {
        std::vector<std::size_t> matches;
        if (pattern_.empty()) {
            matches.reserve(text.size() + 1);
            for (std::size_t index = 0; index <= text.size(); ++index) {
                matches.push_back(index);
            }
            return matches;
        }

        std::size_t matched = 0;
        for (std::size_t index = 0; index < text.size(); ++index) {
            while (matched > 0 && text[index] != pattern_[matched]) {
                matched = prefix_[matched - 1];
            }
            if (text[index] == pattern_[matched]) {
                ++matched;
            }
            if (matched == pattern_.size()) {
                matches.push_back(index + 1 - pattern_.size());
                matched = prefix_[matched - 1];
            }
        }
        return matches;
    }

  private:
    std::string pattern_;
    std::vector<std::size_t> prefix_;
};

} // namespace cp
