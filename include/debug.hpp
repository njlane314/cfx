#pragma once

#include <bits/stdc++.h>

namespace debug_detail {
inline std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

inline std::pair<std::string_view, std::string_view> next_name(std::string_view names) {
    names = trim(names);
    if (names.empty()) {
        return {"?", ""};
    }

    std::size_t pos = names.find(',');
    std::size_t skip = 1;
    if (pos == std::string_view::npos) {
        pos = 0;
        while (pos < names.size() && !std::isspace(static_cast<unsigned char>(names[pos]))) {
            pos++;
        }
        skip = 0;
    }

    std::string_view name = trim(names.substr(0, pos));
    std::string_view rest = pos < names.size() ? names.substr(pos + skip) : std::string_view{};
    return {name.empty() ? std::string_view{"?"} : name, rest};
}

template <class T>
concept DebugIterable =
    requires(const T& value) {
        std::begin(value);
        std::end(value);
    } && !std::is_convertible_v<T, std::string_view>
    && !(std::is_array_v<T> && std::is_same_v<std::remove_cv_t<std::remove_extent_t<T>>, char>);

template <class T>
void write(std::ostream& out, const T& value);

inline void write(std::ostream& out, char value) {
    out << '\'' << value << '\'';
}

inline void write(std::ostream& out, const std::string& value) {
    out << '"' << value << '"';
}

inline void write(std::ostream& out, std::string_view value) {
    out << '"' << value << '"';
}

template <std::size_t N>
void write(std::ostream& out, const char (&value)[N]) {
    out << '"' << value << '"';
}

template <class A, class B>
void write(std::ostream& out, const std::pair<A, B>& value) {
    out << '(';
    write(out, value.first);
    out << ", ";
    write(out, value.second);
    out << ')';
}

template <class... Ts>
void write(std::ostream& out, const std::tuple<Ts...>& value) {
    out << '(';
    if constexpr (sizeof...(Ts) > 0) {
        std::apply(
            [&](const auto&... parts) {
                std::size_t i = 0;
                ((i++ ? out << ", " : out, write(out, parts)), ...);
            },
            value
        );
    }
    out << ')';
}

template <DebugIterable T>
void write(std::ostream& out, const T& value) {
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
}

template <class T>
void write(std::ostream& out, const T& value) {
    out << value;
}

inline void write_items(std::ostream&, std::string_view) {}

template <class T, class... Ts>
void write_items(std::ostream& out, std::string_view names, const T& value, const Ts&... rest) {
    auto [name, remaining] = next_name(names);
    out << name << '=';
    write(out, value);
    if constexpr (sizeof...(rest) > 0) {
        out << ' ';
        write_items(out, remaining, rest...);
    }
}
}  // namespace debug_detail

template <class... Ts>
void debug(std::string_view names, const Ts&... values) {
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
