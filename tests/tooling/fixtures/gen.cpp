#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    const long long seed = argc > 1 ? std::atoll(argv[1]) : 1;
    std::cout << seed % 100 << ' ' << (seed * 17) % 100 << '\n';
}
