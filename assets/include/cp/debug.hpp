#pragma once

#include <cctype>
#include <cstddef>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

// Structured diagnostics for local builds. Calling cp::debug is a no-op unless
// LOCAL is defined; use a call-site macro when arguments must not be evaluated.
namespace cp {
namespace debug_detail {

inline std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

inline std::pair<std::string_view, std::string_view> next_name(std::string_view names) {
    names = trim(names);
    if (names.empty()) {
        return {"?", ""};
    }

    const std::size_t comma = names.find(',');
    if (comma == std::string_view::npos) {
        return {names, ""};
    }
    return {trim(names.substr(0, comma)), names.substr(comma + 1)};
}

template <class T> struct is_pair : std::false_type {};

template <class First, class Second> struct is_pair<std::pair<First, Second>> : std::true_type {};

template <class T, class = void> struct is_tuple_like : std::false_type {};

template <class T>
struct is_tuple_like<T, std::void_t<decltype(std::tuple_size<T>::value)>> : std::true_type {};

template <class T>
concept DebugIterable = requires(const T& value) {
    std::begin(value);
    std::end(value);
};

template <class T> void write(std::ostream& out, const T& value) {
    using Value = std::remove_cvref_t<T>;

    if constexpr (std::is_same_v<Value, char>) {
        out << '\'' << value << '\'';
    } else if constexpr (std::is_convertible_v<const T&, std::string_view>) {
        out << '"' << std::string_view(value) << '"';
    } else if constexpr (is_pair<Value>::value) {
        out << '(';
        write(out, value.first);
        out << ", ";
        write(out, value.second);
        out << ')';
    } else if constexpr (is_tuple_like<Value>::value) {
        out << '(';
        std::size_t index = 0;
        std::apply(
            [&](const auto&... parts) {
                (((index++ == 0 ? void() : static_cast<void>(out << ", ")), write(out, parts)),
                 ...);
            },
            value);
        out << ')';
    } else if constexpr (DebugIterable<T>) {
        out << '[';
        bool first = true;
        for (const auto& item : value) {
            if (!first) {
                out << ", ";
            }
            first = false;
            write(out, item);
        }
        out << ']';
    } else {
        out << value;
    }
}

inline void write_items(std::ostream&, std::string_view) {}

template <class T, class... Rest>
void write_items(std::ostream& out, std::string_view names, const T& value, const Rest&... rest) {
    const auto [name, remaining] = next_name(names);
    out << (name.empty() ? std::string_view{"?"} : name) << '=';
    write(out, value);
    if constexpr (sizeof...(rest) > 0) {
        out << ' ';
        write_items(out, remaining, rest...);
    }
}

} // namespace debug_detail

template <class... Ts> void debug(std::string_view names, const Ts&... values) {
#ifdef LOCAL
    std::cerr << "[debug] ";
    if constexpr (sizeof...(values) == 0) {
        std::cerr << debug_detail::trim(names);
    } else {
        debug_detail::write_items(std::cerr, names, values...);
    }
    std::cerr << '\n';
#else
    (void)names;
    ((void)values, ...);
#endif
}

} // namespace cp
