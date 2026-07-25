#define LOCAL
#include "debug.hpp"

#include <cassert>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

int main() {
    std::ostringstream output;
    std::streambuf* previous = std::cerr.rdbuf(output.rdbuf());

    const std::vector<int> values{1, 2, 3};
    const std::tuple<std::string, char, std::pair<int, int>> record{
        "ok",
        'x',
        {4, 5},
    };
    debug("values, record", values, record);
    cp::debug("message", std::string{"hello"});

    std::cerr.rdbuf(previous);
    assert(output.str() == "[debug] values=[1, 2, 3] record=(\"ok\", 'x', (4, 5))\n"
                           "[debug] message=\"hello\"\n");
}
