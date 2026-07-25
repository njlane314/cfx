#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace cfx {

[[nodiscard]] std::string read_text(const std::filesystem::path& path);
void write_text(const std::filesystem::path& path, std::string_view contents);
void write_atomic(const std::filesystem::path& path, std::string_view contents);

} // namespace cfx
