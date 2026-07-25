#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace cfprobs {

class JsonError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class Json {
  public:
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json, std::less<>>;
    using Value = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    Json();
    explicit Json(Value value);

    [[nodiscard]] bool is_null() const;
    [[nodiscard]] bool is_bool() const;
    [[nodiscard]] bool is_number() const;
    [[nodiscard]] bool is_string() const;
    [[nodiscard]] bool is_array() const;
    [[nodiscard]] bool is_object() const;

    [[nodiscard]] bool boolean() const;
    [[nodiscard]] double number() const;
    [[nodiscard]] const std::string& string() const;
    [[nodiscard]] const Array& array() const;
    [[nodiscard]] const Object& object() const;

    [[nodiscard]] const Json* find(std::string_view key) const;
    [[nodiscard]] const Json& at(std::string_view key) const;

  private:
    Value value_;
};

Json parse_json(std::string_view input);
std::string json_quote(std::string_view value);

} // namespace cfprobs
