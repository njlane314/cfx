#pragma once

// Local compatibility shim for Apple clang. GNU libstdc++ provides this header
// on Codeforces; the probs builder finds this one through the local include path.

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <cfenv>
#include <cfloat>
#include <chrono>
#include <cinttypes>
#include <climits>
#include <cmath>
#include <complex>
#include <condition_variable>
#include <csetjmp>
#include <csignal>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <exception>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <ios>
#include <iosfwd>
#include <iostream>
#include <istream>
#include <iterator>
#include <limits>
#include <list>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <optional>
#include <ostream>
#include <queue>
#include <random>
#include <ratio>
#include <regex>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <valarray>
#include <variant>
#include <vector>

#if __has_include(<any>)
#include <any>
#endif
#if __has_include(<bit>)
#include <bit>
#endif
#if __has_include(<charconv>)
#include <charconv>
#endif
#if __has_include(<compare>)
#include <compare>
#endif
#if __has_include(<concepts>)
#include <concepts>
#endif
#if __has_include(<filesystem>)
#include <filesystem>
#endif
#if __has_include(<numbers>)
#include <numbers>
#endif
#if __has_include(<ranges>)
#include <ranges>
#endif
#if __has_include(<span>)
#include <span>
#endif
#if __has_include(<version>)
#include <version>
#endif
