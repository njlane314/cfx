#include "cfprobs/bundle.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cfprobs {
namespace {

namespace fs = std::filesystem;

const std::regex kQuotedInclude(
    R"regex(^[[:space:]]*#[[:space:]]*include[[:space:]]*"([^"]+)"[[:space:]]*(?://.*)?$)regex");
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
        const std::vector<fs::path> candidates = {
            directory / include,
            root_ / "include" / include,
            root_ / include,
        };
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
            if (std::regex_match(line, match, kQuotedInclude)) {
                const auto include_name = match[1].str();
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

Bundler::Bundler(fs::path root) : root_(normalized_root(root)) {}

std::string Bundler::bundle(const fs::path& source) const {
    const auto source_path = source.is_absolute() ? source : root_ / source;
    return BundleRun(root_).run(source_path);
}

std::string bundle(const fs::path& source, const fs::path& root) {
    return Bundler(root).bundle(source);
}

} // namespace cfprobs
