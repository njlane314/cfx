#include "cp/prelude.hpp"

#include <cassert>

namespace {

constexpr bool compile_time_checks() {
    int low = 8;
    int high = 8;
    return cp::chmin(low, 3) && !cp::chmin(low, 5) && cp::chmax(high, 13) && !cp::chmax(high, 9) &&
           low == 3 && high == 13;
}

static_assert(compile_time_checks());
static_assert(sizeof(cp::i64) == 8);
static_assert(sizeof(cp::u64) == 8);

} // namespace

int main() {
    cp::i64 value = 10;
    assert(cp::chmin(value, cp::i64{-4}));
    assert(value == -4);
}
