#include <cp/contract>
#include <cp/debug>

#include <cassert>

int main() {
    int evaluations = 0;
    CP_EXPECT(++evaluations != 0, (++evaluations, "unused"));
    CP_DEBUG(++evaluations);
    assert(evaluations == 0);
}
