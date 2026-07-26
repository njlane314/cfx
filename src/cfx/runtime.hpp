#pragma once

#include <filesystem>

namespace cfx {

// Runtime data is deliberately kept outside the solution archive. An explicit
// root is useful for tests; otherwise each archive receives an isolated
// directory beneath the user's state home.
[[nodiscard]] std::filesystem::path
state_root(const std::filesystem::path& archive_root);

} // namespace cfx
