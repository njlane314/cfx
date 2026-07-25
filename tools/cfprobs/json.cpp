#include "json.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace cfprobs {
namespace {

class Parser {
  public:
    explicit Parser(std::string_view input) : input_(input) {}

    Json parse() {
        Json value = parse_value();
        whitespace();
        if (position_ != input_.size()) {
            fail("unexpected trailing characters");
        }
        return value;
    }

  private:
    Json parse_value() {
        whitespace();
        if (position_ == input_.size()) {
            fail("unexpected end of input");
        }
        switch (input_[position_]) {
        case 'n':
            literal("null");
            return Json{};
        case 't':
            literal("true");
            return Json(Json::Value{true});
        case 'f':
            literal("false");
            return Json(Json::Value{false});
        case '"':
            return Json(Json::Value{parse_string()});
        case '[':
            return Json(Json::Value{parse_array()});
        case '{':
            return Json(Json::Value{parse_object()});
        default:
            if (input_[position_] == '-' ||
                (input_[position_] >= '0' && input_[position_] <= '9')) {
                return Json(Json::Value{parse_number()});
            }
            fail("unexpected character");
        }
    }

    Json::Array parse_array() {
        expect('[');
        Json::Array result;
        whitespace();
        if (consume(']')) {
            return result;
        }
        while (true) {
            result.push_back(parse_value());
            whitespace();
            if (consume(']')) {
                return result;
            }
            expect(',');
        }
    }

    Json::Object parse_object() {
        expect('{');
        Json::Object result;
        whitespace();
        if (consume('}')) {
            return result;
        }
        while (true) {
            whitespace();
            if (position_ == input_.size() || input_[position_] != '"') {
                fail("expected an object key");
            }
            std::string key = parse_string();
            whitespace();
            expect(':');
            const auto [iterator, inserted] = result.emplace(std::move(key), parse_value());
            if (!inserted) {
                fail("duplicate object key");
            }
            (void)iterator;
            whitespace();
            if (consume('}')) {
                return result;
            }
            expect(',');
        }
    }

    std::string parse_string() {
        expect('"');
        std::string result;
        while (position_ < input_.size()) {
            const char character = input_[position_++];
            if (character == '"') {
                return result;
            }
            if (static_cast<unsigned char>(character) < 0x20U) {
                fail("control character in string");
            }
            if (character != '\\') {
                result.push_back(character);
                continue;
            }
            if (position_ == input_.size()) {
                fail("unfinished string escape");
            }
            const char escaped = input_[position_++];
            switch (escaped) {
            case '"':
                result.push_back('"');
                break;
            case '\\':
                result.push_back('\\');
                break;
            case '/':
                result.push_back('/');
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'u':
                append_utf8(result, parse_hex_quad());
                break;
            default:
                fail("unknown string escape");
            }
        }
        fail("unterminated string");
    }

    double parse_number() {
        const std::size_t start = position_;
        if (consume('-')) {
        }
        if (consume('0')) {
        } else {
            digits();
        }
        if (consume('.')) {
            digits();
        }
        if (consume('e') || consume('E')) {
            (void)(consume('+') || consume('-'));
            digits();
        }
        const std::string token(input_.substr(start, position_ - start));
        char* end = nullptr;
        const double result = std::strtod(token.c_str(), &end);
        if (end != token.c_str() + token.size() || !std::isfinite(result)) {
            fail("invalid number");
        }
        return result;
    }

    void digits() {
        const std::size_t start = position_;
        while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
            ++position_;
        }
        if (start == position_) {
            fail("expected a digit");
        }
    }

    std::uint32_t parse_hex_quad() {
        if (position_ + 4 > input_.size()) {
            fail("unfinished unicode escape");
        }
        std::uint32_t value = 0;
        for (int count = 0; count < 4; ++count) {
            const char character = input_[position_++];
            value <<= 4U;
            if (character >= '0' && character <= '9') {
                value |= static_cast<std::uint32_t>(character - '0');
            } else if (character >= 'a' && character <= 'f') {
                value |= static_cast<std::uint32_t>(character - 'a' + 10);
            } else if (character >= 'A' && character <= 'F') {
                value |= static_cast<std::uint32_t>(character - 'A' + 10);
            } else {
                fail("invalid unicode escape");
            }
        }
        return value;
    }

    static void append_utf8(std::string& output, std::uint32_t value) {
        if (value <= 0x7FU) {
            output.push_back(static_cast<char>(value));
        } else if (value <= 0x7FFU) {
            output.push_back(static_cast<char>(0xC0U | (value >> 6U)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        } else {
            output.push_back(static_cast<char>(0xE0U | (value >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        }
    }

    void literal(std::string_view value) {
        if (input_.substr(position_, value.size()) != value) {
            fail("invalid literal");
        }
        position_ += value.size();
    }

    void expect(char value) {
        whitespace();
        if (!consume(value)) {
            fail(std::string("expected '") + value + "'");
        }
    }

    bool consume(char value) {
        if (position_ < input_.size() && input_[position_] == value) {
            ++position_;
            return true;
        }
        return false;
    }

    void whitespace() {
        while (position_ < input_.size()) {
            const char character = input_[position_];
            if (character != ' ' && character != '\n' && character != '\r' && character != '\t') {
                break;
            }
            ++position_;
        }
    }

    [[noreturn]] void fail(const std::string& message) const {
        throw JsonError("invalid JSON at byte " + std::to_string(position_) + ": " + message);
    }

    std::string_view input_;
    std::size_t position_ = 0;
};

} // namespace

Json::Json() : value_(nullptr) {}

Json::Json(Value value) : value_(std::move(value)) {}

bool Json::is_null() const {
    return std::holds_alternative<std::nullptr_t>(value_);
}
bool Json::is_bool() const {
    return std::holds_alternative<bool>(value_);
}
bool Json::is_number() const {
    return std::holds_alternative<double>(value_);
}
bool Json::is_string() const {
    return std::holds_alternative<std::string>(value_);
}
bool Json::is_array() const {
    return std::holds_alternative<Array>(value_);
}
bool Json::is_object() const {
    return std::holds_alternative<Object>(value_);
}

bool Json::boolean() const {
    if (!is_bool()) {
        throw JsonError("JSON value is not a boolean");
    }
    return std::get<bool>(value_);
}

double Json::number() const {
    if (!is_number()) {
        throw JsonError("JSON value is not a number");
    }
    return std::get<double>(value_);
}

const std::string& Json::string() const {
    if (!is_string()) {
        throw JsonError("JSON value is not a string");
    }
    return std::get<std::string>(value_);
}

const Json::Array& Json::array() const {
    if (!is_array()) {
        throw JsonError("JSON value is not an array");
    }
    return std::get<Array>(value_);
}

const Json::Object& Json::object() const {
    if (!is_object()) {
        throw JsonError("JSON value is not an object");
    }
    return std::get<Object>(value_);
}

const Json* Json::find(std::string_view key) const {
    if (!is_object()) {
        return nullptr;
    }
    const auto& values = std::get<Object>(value_);
    const auto iterator = values.find(key);
    return iterator == values.end() ? nullptr : &iterator->second;
}

const Json& Json::at(std::string_view key) const {
    const Json* value = find(key);
    if (value == nullptr) {
        throw JsonError("JSON object has no key '" + std::string(key) + "'");
    }
    return *value;
}

Json parse_json(std::string_view input) {
    return Parser(input).parse();
}

std::string json_quote(std::string_view value) {
    std::ostringstream output;
    output << '"';
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (character < 0x20U) {
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(character);
            } else {
                output << static_cast<char>(character);
            }
        }
    }
    output << '"';
    return output.str();
}

} // namespace cfprobs
