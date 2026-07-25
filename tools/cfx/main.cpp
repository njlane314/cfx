#include "commands.hpp"
#include "problem.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using cfx::cli::Arguments;

fs::path select_root(std::vector<std::string>& values) {
    std::optional<fs::path> explicit_root;
    for (auto iterator = values.begin(); iterator != values.end();) {
        if (*iterator != "--root") {
            ++iterator;
            continue;
        }
        const auto value = std::next(iterator);
        if (value == values.end()) {
            throw std::runtime_error("--root needs a path");
        }
        explicit_root = *value;
        iterator = values.erase(iterator, std::next(value));
    }
    if (explicit_root) {
        return fs::weakly_canonical(*explicit_root);
    }
    if (const char* root = std::getenv("CFX_ROOT"); root != nullptr && *root != '\0') {
        return fs::weakly_canonical(root);
    }
    return cfx::find_workspace_root();
}

int dispatch(std::string command, Arguments arguments, const fs::path& root) {
    using namespace cfx::cli;
    if (command == "get") return command_get(std::move(arguments), root);
    if (command == "test") return command_test(std::move(arguments), root);
    if (command == "bundle") return command_bundle(std::move(arguments), root);
    if (command == "stress") return command_stress(std::move(arguments), root);
    if (command == "fail") return command_fail(std::move(arguments), root);
    if (command == "cc") return command_cc(std::move(arguments), root);
    if (command == "submit") return command_submit(std::move(arguments), root);

    std::vector<std::string> problem{std::move(command)};
    while (!arguments.empty()) problem.push_back(arguments.take());
    return command_problem(problem, root);
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::vector<std::string> values(argv + 1, argv + argc);
        const fs::path root = select_root(values);
        Arguments arguments(std::move(values));
        if (arguments.empty()) {
            cfx::cli::show_help();
            return 0;
        }

        const std::string command = arguments.take();
        if (command == "--help" || command == "-h") {
            cfx::cli::show_help();
            return 0;
        }
        if (command == "help") {
            if (arguments.empty()) {
                cfx::cli::show_help();
            } else {
                cfx::cli::show_command_help(arguments.take());
                if (!arguments.empty()) {
                    throw std::runtime_error("help accepts one command name");
                }
            }
            return 0;
        }
        return dispatch(command, std::move(arguments), root);
    } catch (const std::exception& error) {
        std::cerr << "cfx: " << error.what() << '\n';
        return 2;
    }
}
