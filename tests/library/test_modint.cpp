#include "cp/math/modint.hpp"

#include <cassert>
#include <cstdint>
#include <random>
#include <stdexcept>

namespace {

using mint = cp::modint<1'000'000'007U>;

static_assert(mint::modulus() == 1'000'000'007U);
static_assert(mint{-1}.value() == 1'000'000'006U);
static_assert((mint{10} + mint{20}).value() == 30);
static_assert((mint{2}.pow(10)).value() == 1'024);

} // namespace

int main() {
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
        if (second != 0) {
            assert(((a / b) * b) == a);
        }
    }

    using composite = cp::modint<12>;
    assert(composite{5}.inverse().value() == 5);
    bool rejected = false;
    try {
        static_cast<void>(composite{6}.inverse());
    } catch (const std::domain_error&) {
        rejected = true;
    }
    assert(rejected);

    using wide = cp::modint<4'294'967'291U>;
    const wide minus_one = 4'294'967'290ULL;
    assert((minus_one * minus_one).value() == 1);
}
