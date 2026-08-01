#define LOCAL

#include <cp/contract>
#include <cp/debug>
#include <cp/disjoint_set>
#include <cp/fenwick_tree>
#include <cp/kmp>
#include <cp/modint>
#include <cp/segment_tree>
#include <cp/types>
#include <cp/utility>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

constexpr bool utility_checks() {
    int low = 8;
    int high = 8;
    return cp::chmin(low, 3) && !cp::chmin(low, 5) && cp::chmax(high, 13) &&
           !cp::chmax(high, 9) && low == 3 && high == 13;
}

using mint = cp::modint<1'000'000'007U>;
static_assert(utility_checks());
static_assert(cp::npos == -1 && sizeof(cp::i64) == 8 && sizeof(cp::u64) == 8);
static_assert(mint::modulus() == 1'000'000'007U);
static_assert(mint{-1}.value() == 1'000'000'006U);
static_assert((mint{10} + mint{20}).value() == 30);
static_assert(mint{2}.pow(10).value() == 1'024);
#if defined(__SIZEOF_INT128__)
constexpr cp::u128 huge = (static_cast<cp::u128>(1) << 100) + 7;
constexpr auto huge_remainder = static_cast<std::uint32_t>(huge % mint::modulus());
static_assert(mint{huge}.value() == huge_remainder);
static_assert(mint{-static_cast<cp::i128>(huge)}.value() == mint::modulus() - huge_remainder);
#endif

std::size_t offset(cp::index_type index) { return static_cast<std::size_t>(index); }

cp::index_type pick(std::mt19937& random, cp::index_type bound) {
    return static_cast<cp::index_type>(random() % static_cast<std::uint32_t>(bound));
}

void test_contract_and_debug() {
    int message_calls = 0;
    CP_EXPECT(2 + 2 == 4, (++message_calls, "unused"));
    assert(message_calls == 0);

    std::ostringstream output;
    std::streambuf* previous = std::cerr.rdbuf(output.rdbuf());
    const std::string text = "a\n\"b";
    const std::pair<int, bool> pair{4, true};
    const std::tuple<std::string, char> tuple{"ok", 'x'};
    std::vector<int> range(65);
    std::iota(range.begin(), range.end(), 0);
    const char* missing = nullptr;
    CP_DEBUG(text, pair, tuple, range, missing);
    std::cerr.rdbuf(previous);

    const std::string rendered = output.str();
    assert(rendered.starts_with("[test_library.cpp:"));
    assert(rendered.find("text, pair, tuple, range, missing = (") != std::string::npos);
    assert(rendered.find("\"a\\n\\\"b\"") != std::string::npos);
    assert(rendered.find("(4, true)") != std::string::npos);
    assert(rendered.find("(\"ok\", 'x')") != std::string::npos);
    assert(rendered.find(", ...]") != std::string::npos);
    assert(rendered.find("nullptr") != std::string::npos);
}

void test_disjoint_set() {
    cp::disjoint_set empty;
    assert(empty.empty() && empty.size() == 0 && empty.component_count() == 0);

    cp::disjoint_set sample(5);
    sample.merge(0, 2);
    sample.merge(3, 4);
    assert(sample.groups() ==
           std::vector<std::vector<cp::index_type>>({{0, 2}, {1}, {3, 4}}));

    constexpr cp::index_type size = 41;
    cp::disjoint_set sets(size);
    std::vector<cp::index_type> label(offset(size));
    std::iota(label.begin(), label.end(), 0);
    std::mt19937 random(0xD515EA5EU);

    for (int step = 0; step < 5'000; ++step) {
        const cp::index_type first = pick(random, size);
        const cp::index_type second = pick(random, size);
        const bool were_same = label[offset(first)] == label[offset(second)];
        if (step % 3 == 0) {
            assert(sets.same(first, second) == were_same);
        } else {
            assert(sets.merge(first, second) == !were_same);
            if (!were_same) {
                const auto replaced = label[offset(second)];
                const auto replacement = label[offset(first)];
                std::replace(label.begin(), label.end(), replaced, replacement);
            }
        }

        const cp::index_type element = pick(random, size);
        const auto expected = std::count(label.begin(), label.end(), label[offset(element)]);
        assert(sets.component_size(element) == static_cast<cp::index_type>(expected));
        std::vector<cp::index_type> distinct = label;
        std::sort(distinct.begin(), distinct.end());
        distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
        assert(sets.component_count() == static_cast<cp::index_type>(distinct.size()));
    }
}

long long naive_sum(const std::vector<long long>& values, cp::index_type first,
                    cp::index_type last) {
    long long result = 0;
    for (cp::index_type index = first; index < last; ++index) result += values[offset(index)];
    return result;
}

void test_fenwick_tree() {
    cp::fenwick_tree<long long> empty;
    assert(empty.empty() && empty.prefix_sum(0) == 0);

    constexpr cp::index_type size = 67;
    std::vector<long long> values(offset(size));
    for (cp::index_type index = 0; index < size; ++index) values[offset(index)] = index % 7 - 3;
    cp::fenwick_tree<long long> tree(values);
    std::mt19937 random(0xF311B17U);

    for (int step = 0; step < 8'000; ++step) {
        const cp::index_type position = pick(random, size);
        if (step % 3 == 0) {
            const long long delta = static_cast<long long>(random() % 2'001U) - 1'000;
            values[offset(position)] += delta;
            tree.add(position, delta);
        } else if (step % 3 == 1) {
            const long long value = static_cast<long long>(random() % 2'001U) - 1'000;
            values[offset(position)] = value;
            tree.set(position, value);
        } else {
            cp::index_type first = static_cast<cp::index_type>(random() % (size + 1));
            cp::index_type last = static_cast<cp::index_type>(random() % (size + 1));
            if (first > last) std::swap(first, last);
            assert(tree.prefix_sum(last) == naive_sum(values, 0, last));
            assert(tree.sum(first, last) == naive_sum(values, first, last));
            assert(tree.get(position) == values[offset(position)]);
        }
    }
}

struct concatenate {
    std::string operator()(const std::string& left, const std::string& right) const {
        return left + right;
    }
};

std::string naive_fold(const std::vector<std::string>& values, cp::index_type first,
                       cp::index_type last) {
    std::string result;
    for (cp::index_type index = first; index < last; ++index) result += values[offset(index)];
    return result;
}

void test_segment_tree() {
    cp::segment_tree<std::string, concatenate> empty(0, "", {});
    assert(empty.empty() && empty.fold(0, 0).empty() && empty.fold_all().empty());

    constexpr cp::index_type size = 37;
    std::vector<std::string> values(offset(size));
    for (cp::index_type index = 0; index < size; ++index)
        values[offset(index)] = std::string(1, static_cast<char>('a' + index % 5));
    cp::segment_tree<std::string, concatenate> tree(values, "", {});
    std::mt19937 random(0x5E67A11U);

    for (int step = 0; step < 6'000; ++step) {
        if (step % 3 == 0) {
            const cp::index_type position = pick(random, size);
            values[offset(position)] =
                std::string(1, static_cast<char>('A' + random() % 26U));
            tree.set(position, values[offset(position)]);
            assert(tree.get(position) == values[offset(position)]);
        } else {
            cp::index_type first = static_cast<cp::index_type>(random() % (size + 1));
            cp::index_type last = static_cast<cp::index_type>(random() % (size + 1));
            if (first > last) std::swap(first, last);
            assert(tree.fold(first, last) == naive_fold(values, first, last));
            assert(tree.fold_all() == naive_fold(values, 0, size));
        }
    }
}

void test_modint() {
    constexpr std::uint64_t modulus = mint::modulus();
    std::mt19937_64 random(0xBADC0FFEEULL);
    for (int step = 0; step < 10'000; ++step) {
        const std::uint64_t raw_first = random();
        const std::uint64_t raw_second = random();
        const std::uint64_t first = raw_first % modulus;
        const std::uint64_t second = raw_second % modulus;
        const mint a = raw_first;
        const mint b = raw_second;
        assert((a + b).value() == (first + second) % modulus);
        assert((a - b).value() == (first + modulus - second) % modulus);
        assert((a * b).value() == first * second % modulus);
        if (second != 0) assert((a / b) * b == a);
    }
    assert(cp::modint<12>{5}.inverse().value() == 5);
    using wide = cp::modint<4'294'967'291U>;
    const wide minus_one = 4'294'967'290ULL;
    assert((minus_one * minus_one).value() == 1);
}

std::vector<cp::index_type> brute_matches(std::string_view text, std::string_view pattern) {
    std::vector<cp::index_type> matches;
    for (std::size_t index = 0; index + pattern.size() <= text.size(); ++index)
        if (text.substr(index, pattern.size()) == pattern)
            matches.push_back(static_cast<cp::index_type>(index));
    return matches;
}

std::string random_string(std::mt19937& random, std::size_t length) {
    std::string result(length, 'a');
    for (char& character : result) character = static_cast<char>('a' + random() % 3U);
    return result;
}

void test_kmp() {
    const cp::kmp_matcher matcher("ababcabab");
    assert(matcher.prefix_table() ==
           std::vector<cp::index_type>({0, 0, 1, 2, 0, 1, 2, 3, 4}));
    assert(cp::kmp_matcher("aaa").find_all("aaaaa") ==
           std::vector<cp::index_type>({0, 1, 2}));
    assert(cp::kmp_matcher("").find_all("abc") ==
           std::vector<cp::index_type>({0, 1, 2, 3}));

    std::mt19937 random(0x4B4D50U);
    for (int step = 0; step < 10'000; ++step) {
        const std::string text = random_string(random, random() % 31U);
        const std::string pattern = random_string(random, random() % 11U);
        assert(cp::kmp_matcher(pattern).find_all(text) == brute_matches(text, pattern));
    }
}

int contract_case(std::string_view name) {
    if (name == "disjoint-size") {
        const cp::disjoint_set invalid(-1);
        static_cast<void>(invalid);
    } else if (name == "fenwick-index") {
        static_cast<void>(cp::fenwick_tree<int>(1).get(-1));
    } else if (name == "segment-range") {
        static_cast<void>(cp::segment_tree<int>(1).fold(1, 0));
    } else if (name == "modint-inverse") {
        static_cast<void>(cp::modint<12>{6}.inverse());
    }
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2) return contract_case(argv[1]);
    test_contract_and_debug();
    test_disjoint_set();
    test_fenwick_tree();
    test_segment_tree();
    test_modint();
    test_kmp();
}
