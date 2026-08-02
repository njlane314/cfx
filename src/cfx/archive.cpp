#include "cfx.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <unistd.h>
#include <utility>
#include <vector>

namespace cfx {

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output) {
        throw std::runtime_error("cannot write " + path.string());
    }
}

void write_atomic(const std::filesystem::path& path, std::string_view contents) {
    if (std::filesystem::is_regular_file(path) && read_text(path) == contents) {
        return;
    }

    const std::filesystem::path temporary = path.string() + ".tmp." + std::to_string(::getpid());
    try {
        write_text(temporary, contents);
        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        if (error) {
            throw std::runtime_error("cannot replace " + path.string() + ": " + error.message());
        }
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

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

namespace {

namespace fs = std::filesystem;

template <class Error>
fs::path resolve_path(const fs::path& path, std::string_view description) {
    std::error_code error;
    const fs::path absolute = fs::absolute(path, error);
    if (error) {
        throw Error("cannot resolve " + std::string(description) + " '" + path.string() +
                    "': " + error.message());
    }
    const fs::path canonical = fs::weakly_canonical(absolute, error);
    return error ? absolute.lexically_normal() : canonical;
}

fs::path runtime_path(const fs::path& path) {
    return resolve_path<std::runtime_error>(path, "runtime path");
}

fs::path problem_path(const fs::path& path) {
    return resolve_path<ProblemError>(path, "path");
}

fs::path workspace_path(const fs::path& path) {
    return resolve_path<WorkspaceError>(path, "workspace root");
}

std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
}

std::string safe_name(std::string value) {
    for (char& character : value) {
        const bool safe =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' || character == '_';
        if (!safe) {
            character = '-';
        }
    }
    return value.empty() ? "archive" : value;
}

std::string archive_key(const fs::path& archive_root) {
    const fs::path canonical = runtime_path(archive_root);
    return safe_name(canonical.filename().string()) + "-" +
           content_digest(canonical.string()).substr(0, 12);
}

fs::path home_directory() {
    const std::string home = environment("HOME");
    if (home.empty()) {
        throw std::runtime_error("HOME is not set; configure an explicit cfx runtime root");
    }
    return runtime_path(home);
}

fs::path selected_root(const fs::path& archive_root) {
    const std::string explicit_root = environment("CFX_STATE_ROOT");
    if (!explicit_root.empty()) {
        return runtime_path(explicit_root);
    }

    const std::string xdg_root = environment("XDG_STATE_HOME");
    const fs::path base = xdg_root.empty() ? home_directory() / ".local/state/cfx"
                                           : runtime_path(xdg_root) / "cfx";
    return base / "workspaces" / archive_key(archive_root);
}

} // namespace

fs::path state_root(const fs::path& archive_root) {
    return selected_root(archive_root);
}

std::string content_digest(std::string_view value) {
    std::uint64_t first = 14695981039346656037ULL;
    std::uint64_t second = 1099511628211ULL;
    for (const unsigned char byte : value) {
        first = (first ^ byte) * 1099511628211ULL;
        second ^= static_cast<std::uint64_t>(byte) + 0x9e3779b97f4a7c15ULL;
        second *= 14029467366897019727ULL;
        second ^= second >> 31U;
    }
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << first << std::setw(16) << second;
    return stream.str();
}

namespace {

namespace fs = std::filesystem;

bool valid_contest(std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](char c) {
        return c >= '0' && c <= '9';
    });
}

bool valid_index(std::string_view value) {
    if (value.empty() || value.front() < 'A' || value.front() > 'Z') {
        return false;
    }
    return std::all_of(value.begin() + 1, value.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    });
}

std::vector<std::string> components(const fs::path& path) {
    std::vector<std::string> result;
    for (const auto& part : path) {
        result.push_back(part.string());
    }
    return result;
}

bool is_beneath(const fs::path& path, const fs::path& root) {
    auto path_part = path.begin();
    for (auto root_part = root.begin(); root_part != root.end(); ++root_part, ++path_part) {
        if (path_part == path.end() || *path_part != *root_part) {
            return false;
        }
    }
    return true;
}

std::optional<Problem> infer_canonical_layout(const fs::path& location, const fs::path& root) {
    const auto parts = components(location.lexically_relative(root));
    if (parts.size() >= 3 && parts[0] == "codeforces" && valid_contest(parts[1]) &&
        valid_index(parts[2])) {
        return Problem(parts[1], parts[2], root);
    }
    return std::nullopt;
}

} // namespace

Problem::Problem(std::string contest_id, std::string index, fs::path root)
    : contest_id_(std::move(contest_id)), index_(std::move(index)),
      root_(problem_path(root)) {
    if (!valid_contest(contest_id_) || !valid_index(index_)) {
        throw ProblemError("invalid Codeforces problem; expected a numeric contest and an "
                           "index beginning with a letter");
    }
}

Problem Problem::parse(std::string_view value, const fs::path& root) {
    const std::size_t split = value.find_first_not_of("0123456789");
    if (split != 0 && split != std::string_view::npos) {
        return Problem(std::string(value.substr(0, split)), std::string(value.substr(split)), root);
    }
    throw ProblemError("cannot parse problem '" + std::string(value) +
                       "'; expected an ID like 71A");
}

std::optional<Problem> Problem::infer(const fs::path& location, const fs::path& root) {
    const auto normalized_root = problem_path(root);
    const auto absolute_location = location.is_absolute() ? location : normalized_root / location;
    const auto normalized_location = problem_path(absolute_location);
    if (!is_beneath(normalized_location, normalized_root)) {
        return std::nullopt;
    }
    return infer_canonical_layout(normalized_location, normalized_root);
}

const std::string& Problem::contest_id() const noexcept {
    return contest_id_;
}

const std::string& Problem::index() const noexcept {
    return index_;
}

std::string Problem::id() const {
    return contest_id_ + index_;
}

fs::path Problem::directory() const {
    return root_ / "codeforces" / contest_id_ / index_;
}

fs::path Problem::solution_path() const {
    return directory() / "solution.cpp";
}

fs::path Problem::metadata_path() const {
    return directory() / "problem.json";
}

fs::path Problem::state_directory() const {
    return cfx::state_root(root_) / "codeforces" / contest_id_ / index_;
}

fs::path Problem::samples_path() const {
    return state_directory() / "samples";
}

fs::path Problem::cases_path() const {
    return directory() / "cases";
}

std::vector<fs::path> Problem::test_directories() const {
    return {samples_path(), cases_path()};
}

fs::path find_workspace_root(const fs::path& start) {
    auto current = problem_path(start);
    std::error_code error;
    if (fs::is_regular_file(current, error)) {
        current = current.parent_path();
    }

    while (!current.empty()) {
        const bool has_git = fs::exists(current / ".git");
        const bool has_archive = fs::exists(current / "codeforces");
        if (has_git || has_archive) {
            return current;
        }
        const auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return problem_path(start);
}

namespace {

namespace fs = std::filesystem;

fs::path select_template(const fs::path& root) {
    const auto local = root / ".cfx" / "solution.cpp";
    if (fs::exists(local)) {
        return local;
    }
    const char* configured = std::getenv("CFX_SOLUTION_TEMPLATE");
    const fs::path preferred = configured != nullptr && *configured != '\0'
                                   ? fs::path(configured)
                                   : root / "solution.cpp";
    if (fs::is_regular_file(preferred)) {
        return preferred;
    }
    throw WorkspaceError("solution template not found; expected '" + preferred.string() + "'");
}

fs::path current_problem_path(const fs::path& root) {
    const fs::path archive = workspace_path(root);
    return cfx::state_root(archive) / "current-problem";
}

} // namespace

Workspace::Workspace(fs::path root) : root_(workspace_path(std::move(root))) {}

fs::path Workspace::create(const Problem& problem) const {
    Problem local(problem.contest_id(), problem.index(), root_);
    const fs::path solution = local.solution_path();

    const std::vector<fs::path> directories = {
        local.directory(),
        local.samples_path(),
        local.cases_path(),
    };
    for (const auto& directory : directories) {
        std::error_code error;
        fs::create_directories(directory, error);
        if (error) {
            throw WorkspaceError("cannot create directory '" + directory.string() +
                                 "': " + error.message());
        }
    }

    if (!fs::exists(solution)) {
        const auto source_template = select_template(root_);
        if (!fs::is_regular_file(source_template)) {
            throw WorkspaceError("solution template is not a file: '" + source_template.string() +
                                 "'");
        }
        try {
            write_text(solution, read_text(source_template));
        } catch (const std::exception& error) {
            throw WorkspaceError(error.what());
        }
    } else if (!fs::is_regular_file(solution)) {
        throw WorkspaceError("solution path is not a file: '" + solution.string() + "'");
    }

    return solution;
}

void remember_current_problem(const Problem& problem, const fs::path& root) {
    const fs::path path = current_problem_path(root);
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error) {
        throw WorkspaceError("cannot create current problem directory '" +
                             path.parent_path().string() + "': " + error.message());
    }

    try {
        write_atomic(path, problem.id() + '\n');
    } catch (const std::exception& failure) {
        throw WorkspaceError(failure.what());
    }
}

std::optional<Problem> current_problem(const fs::path& root) {
    const fs::path path = current_problem_path(root);
    std::error_code error;
    const bool exists = fs::exists(path, error);
    if (error) {
        throw WorkspaceError("cannot inspect current problem record '" + path.string() +
                             "': " + error.message());
    }
    if (!exists) {
        return std::nullopt;
    }

    std::string contents;
    try {
        contents = read_text(path);
    } catch (const std::exception& failure) {
        throw WorkspaceError(failure.what());
    }
    if (contents.size() >= 65) {
        throw WorkspaceError("invalid current problem record '" + path.string() +
                             "'; run cfx PROBLEM again");
    }
    if (contents.empty() || contents.back() != '\n') {
        throw WorkspaceError("invalid current problem record '" + path.string() +
                             "'; run cfx PROBLEM again");
    }
    contents.pop_back();
    try {
        Problem problem = Problem::parse(contents, root);
        if (problem.id() != contents) {
            throw ProblemError("current problem record is not canonical");
        }
        return problem;
    } catch (const ProblemError&) {
        throw WorkspaceError("invalid current problem record '" + path.string() +
                             "'; run cfx PROBLEM again");
    }
}

namespace {

namespace fs = std::filesystem;

const std::regex kBundledInclude(
    R"regex(^[[:space:]]*#[[:space:]]*include[[:space:]]*(?:"([^"]+)"|<(cp/[^>]+|peek\.hpp)>)[[:space:]]*(?://.*)?$)regex");
const std::regex
    kPragmaOnce(R"(^[[:space:]]*#[[:space:]]*pragma[[:space:]]+once[[:space:]]*(?://.*)?$)");

fs::path normalized_root(const fs::path& root) {
    std::error_code error;
    auto absolute = fs::absolute(root, error);
    if (error) {
        throw BundleError("bundle: cannot resolve workspace root '" + root.string() +
                          "': " + error.message());
    }
    auto canonical = fs::weakly_canonical(absolute, error);
    return error ? absolute.lexically_normal() : canonical;
}

fs::path canonical_file(const fs::path& path, std::string_view description) {
    std::error_code error;
    const auto canonical = fs::canonical(path, error);
    if (error || !fs::is_regular_file(canonical)) {
        const auto detail = error ? ": " + error.message() : "";
        throw BundleError("bundle: " + std::string(description) + " not found: '" + path.string() +
                          "'" + detail);
    }
    return canonical;
}

std::string display_path(const fs::path& path, const fs::path& root) {
    std::error_code error;
    const auto relative = fs::relative(path, root, error);
    if (!error && !relative.empty() && *relative.begin() != fs::path("..")) {
        return relative.generic_string();
    }
    return path.generic_string();
}

class BundleRun {
  public:
    explicit BundleRun(fs::path root) : root_(std::move(root)) {}

    std::string run(const fs::path& source) {
        const auto canonical = canonical_file(source, "source");
        seen_.insert(canonical);
        return expand(canonical);
    }

  private:
    fs::path resolve_include(std::string_view include, const fs::path& directory) const {
        std::vector<fs::path> candidates{directory / include};
        if (include == "peek.hpp") {
            candidates.push_back(root_ / "include" / "peek" / include);
        }
        candidates.push_back(root_ / "include" / include);
        candidates.push_back(root_ / include);
        std::vector<std::string> searched;
        for (const auto& candidate : candidates) {
            std::error_code error;
            if (fs::is_regular_file(candidate, error) && !error) {
                return canonical_file(candidate, "include");
            }
            searched.push_back(candidate.string());
        }

        std::ostringstream message;
        message << "bundle: cannot resolve include \"" << include << "\" from '"
                << directory.string() << "'; searched ";
        for (std::size_t i = 0; i < searched.size(); ++i) {
            if (i != 0) {
                message << ", ";
            }
            message << '\'' << searched[i] << '\'';
        }
        throw BundleError(message.str());
    }

    std::string expand(const fs::path& source) {
        stack_.push_back(source);
        struct PopStack {
            std::vector<fs::path>& stack;
            ~PopStack() {
                stack.pop_back();
            }
        } pop{stack_};

        std::ifstream input(source);
        if (!input) {
            throw BundleError("bundle: cannot read '" + source.string() + "'");
        }

        std::ostringstream output;
        std::string line;
        while (std::getline(input, line)) {
            std::smatch match;
            if (std::regex_match(line, match, kBundledInclude)) {
                const auto include_name = (match[1].matched ? match[1] : match[2]).str();
                const auto include = resolve_include(include_name, source.parent_path());
                const auto cycle = std::find(stack_.begin(), stack_.end(), include);
                if (cycle != stack_.end()) {
                    std::ostringstream message;
                    message << "bundle: include cycle: ";
                    for (auto item = cycle; item != stack_.end(); ++item) {
                        if (item != cycle) {
                            message << " -> ";
                        }
                        message << display_path(*item, root_);
                    }
                    message << " -> " << display_path(include, root_);
                    throw BundleError(message.str());
                }
                if (!seen_.insert(include).second) {
                    continue;
                }

                output << "\n// ===== BEGIN " << include_name << " =====\n";
                output << expand(include);
                output << "// ===== END " << include_name << " =====\n\n";
            } else if (!std::regex_match(line, kPragmaOnce)) {
                output << line << '\n';
            }
        }
        if (input.bad()) {
            throw BundleError("bundle: cannot finish reading '" + source.string() + "'");
        }
        return output.str();
    }

    fs::path root_;
    std::unordered_set<fs::path> seen_;
    std::vector<fs::path> stack_;
};

} // namespace

std::string bundle(const fs::path& source, const fs::path& root) {
    const fs::path normalized = normalized_root(root);
    return BundleRun(normalized).run(source.is_absolute() ? source : normalized / source);
}

namespace {

namespace fs = std::filesystem;

std::string with_final_newline(std::string value) {
    if (value.empty() || value.back() != '\n') {
        value.push_back('\n');
    }
    return value;
}

int integer_field(const Json& object, std::string_view name) {
    const Json* value = object.find(name);
    if (value == nullptr) {
        return 0;
    }
    if (!value->is_number() || value->number() < 0.0 ||
        value->number() > static_cast<double>(std::numeric_limits<int>::max()) ||
        std::trunc(value->number()) != value->number()) {
        throw std::runtime_error("problem package field '" + std::string(name) +
                                 "' must be a non-negative integer");
    }
    return static_cast<int>(value->number());
}

std::string string_field(const Json& object, std::string_view name) {
    const Json* value = object.find(name);
    return value != nullptr && value->is_string() ? value->string() : std::string{};
}

std::string sample_name(std::size_t number, const char* extension) {
    std::ostringstream output;
    output << std::setw(2) << std::setfill('0') << number << extension;
    return output.str();
}

void recover_sample_transaction(const Problem& problem) {
    const fs::path directory = problem.state_directory();
    if (!fs::is_directory(directory)) {
        return;
    }
    const fs::path samples = problem.samples_path();
    const fs::path backup = directory / ".samples.backup";
    std::error_code error;
    if (fs::is_directory(backup)) {
        if (!fs::exists(samples)) {
            fs::rename(backup, samples, error);
            if (error) {
                throw std::runtime_error("cannot recover fetched samples: " + error.message());
            }
        } else {
            fs::remove_all(backup, error);
        }
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
        const std::string name = entry.path().filename().string();
        if (entry.is_directory() && name.starts_with(".samples.stage.")) {
            fs::remove_all(entry.path(), error);
        }
    }
}

Problem problem_from_url(std::string_view url, const fs::path& root) {
    constexpr std::string_view origin = "https://codeforces.com/";
    if (!url.starts_with(origin)) {
        throw std::runtime_error("problem package has an invalid URL");
    }
    url.remove_prefix(origin.size());
    if (url.ends_with('/')) url.remove_suffix(1);

    std::string_view contest;
    std::string index;
    if (url.starts_with("contest/")) {
        url.remove_prefix(8);
        const std::size_t split = url.find("/problem/");
        contest = url.substr(0, split);
        index = split == std::string_view::npos ? "" : std::string(url.substr(split + 9));
    } else if (url.starts_with("problemset/problem/")) {
        url.remove_prefix(19);
        const std::size_t split = url.find('/');
        contest = url.substr(0, split);
        index = split == std::string_view::npos ? "" : std::string(url.substr(split + 1));
    }
    std::transform(index.begin(), index.end(), index.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    try {
        return Problem(std::string(contest), std::move(index), root);
    } catch (const ProblemError&) {
        throw std::runtime_error("problem package has an invalid URL");
    }
}

} // namespace

CompanionPackage parse_companion_package(std::string_view payload, const fs::path& root) {
    const Json document = parse_json(payload);
    const std::string url = string_field(document, "url");
    if (url.empty()) {
        throw std::runtime_error("problem package has no URL");
    }
    CompanionPackage package{
        problem_from_url(url, root),
        url,
        string_field(document, "name"),
        integer_field(document, "timeLimit"),
        integer_field(document, "memoryLimit"),
        {},
    };
    const Json* tests = document.find("tests");
    if (tests == nullptr || !tests->is_array()) {
        throw std::runtime_error("problem package has no tests");
    }
    for (const Json& test : tests->array()) {
        package.samples.push_back(Sample{
            string_field(test, "input"),
            string_field(test, "output"),
        });
    }
    if (package.samples.empty()) {
        throw std::runtime_error("problem package has no tests");
    }
    return package;
}

void import_companion_package(const CompanionPackage& package, const fs::path& root) {
    recover_sample_transaction(package.problem);
    Workspace(root).create(package.problem);
    const fs::path samples = package.problem.samples_path();

    struct Expected {
        std::string input_name;
        std::string output_name;
        std::string input;
        std::string output;
    };
    std::vector<Expected> expected;
    for (std::size_t index = 0; index < package.samples.size(); ++index) {
        expected.push_back(Expected{
            sample_name(index + 1, ".in"),
            sample_name(index + 1, ".out"),
            with_final_newline(package.samples[index].input),
            with_final_newline(package.samples[index].output),
        });
    }

    bool identical = true;
    for (const fs::directory_entry& entry : fs::directory_iterator(samples)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() == ".in" || entry.path().extension() == ".out") {
            const std::string name = entry.path().filename().string();
            const bool known =
                std::any_of(expected.begin(), expected.end(), [&](const Expected& value) {
                    return value.input_name == name || value.output_name == name;
                });
            if (!known) {
                identical = false;
            }
        }
    }
    for (const Expected& pair : expected) {
        const fs::path input = samples / pair.input_name;
        const fs::path output = samples / pair.output_name;
        if (!fs::is_regular_file(input) || !fs::is_regular_file(output) ||
            read_text(input) != pair.input || read_text(output) != pair.output) {
            identical = false;
        }
    }

    if (!identical) {
        const fs::path state = package.problem.state_directory();
        const fs::path stage = state / (".samples.stage." + std::to_string(::getpid()));
        const fs::path backup = state / ".samples.backup";
        std::error_code ignored;
        fs::remove_all(stage, ignored);
        fs::remove_all(backup, ignored);
        fs::create_directories(stage);
        try {
            for (const Expected& pair : expected) {
                write_text(stage / pair.input_name, pair.input);
                write_text(stage / pair.output_name, pair.output);
            }
            fs::rename(samples, backup);
            try {
                fs::rename(stage, samples);
            } catch (...) {
                fs::rename(backup, samples);
                throw;
            }
            fs::remove_all(backup, ignored);
        } catch (...) {
            fs::remove_all(stage, ignored);
            if (!fs::exists(samples) && fs::exists(backup)) {
                std::error_code restore_error;
                fs::rename(backup, samples, restore_error);
            }
            throw;
        }
    }

    const fs::path metadata = package.problem.metadata_path();
    write_atomic(metadata, "{\n"
                           "  \"id\": " +
                               json_quote(package.problem.id()) +
                               ",\n"
                               "  \"name\": " +
                               json_quote(package.name) +
                               ",\n"
                               "  \"url\": " +
                               json_quote(package.url) +
                               ",\n"
                               "  \"timeLimitMs\": " +
                               std::to_string(package.time_limit_ms) +
                               ",\n"
                               "  \"memoryLimitMb\": " +
                               std::to_string(package.memory_limit_mb) +
                               "\n"
                               "}\n");
}

} // namespace cfx
