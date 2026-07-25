#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

namespace cfx {

class BundleError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class Bundler {
  public:
    explicit Bundler(std::filesystem::path root = std::filesystem::current_path());

    [[nodiscard]] std::string bundle(const std::filesystem::path& source) const;

  private:
    std::filesystem::path root_;
};

[[nodiscard]] std::string
bundle(const std::filesystem::path& source,
       const std::filesystem::path& root = std::filesystem::current_path());

} // namespace cfx
