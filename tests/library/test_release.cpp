#include <cp/contract>
#include <cp/debug>
#include <cp/modint>

#include <cassert>

int main() {
    int evaluations = 0;
    CP_EXPECT(++evaluations != 0, (++evaluations, "unused"));
    CP_DEBUG(++evaluations);
    assert(evaluations == 0);
    assert(cp::modint<12>{5}.inverse().value() == 5);
    assert((cp::modint<12>{7} / cp::modint<12>{5}).value() == 11);
}
