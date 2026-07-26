#pragma once

#include <filesystem>

namespace cfx {

// Resolve read-only templates, headers, and connector metadata independently
// from the writable solution archive.
[[nodiscard]] std::filesystem::path
asset_root(const std::filesystem::path& fallback = std::filesystem::current_path());

} // namespace cfx
