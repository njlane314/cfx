#pragma once

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cfx::cli {

class Arguments {
  public:
    explicit Arguments(std::vector<std::string> values) : values_(std::move(values)) {}

    [[nodiscard]] bool empty() const noexcept {
        return position_ >= values_.size();
    }

    std::string take() {
        if (empty()) {
            throw std::runtime_error("missing command argument");
        }
        return values_[position_++];
    }

  private:
    std::vector<std::string> values_;
    std::size_t position_ = 0;
};

void show_help();
void show_command_help(const std::string& command);

int command_get(Arguments arguments, const std::filesystem::path& root);
int command_problem(const std::vector<std::string>& values,
                    const std::filesystem::path& root);
int command_test(Arguments arguments, const std::filesystem::path& root);
int command_bundle(Arguments arguments, const std::filesystem::path& root);
int command_stress(Arguments arguments, const std::filesystem::path& root);
int command_fail(Arguments arguments, const std::filesystem::path& root);
int command_cc(Arguments arguments, const std::filesystem::path& root);
int command_submit(Arguments arguments, const std::filesystem::path& root);

} // namespace cfx::cli
