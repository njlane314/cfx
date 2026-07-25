#include "cfprobs/workspace.hpp"

#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace cfprobs {
namespace {

namespace fs = std::filesystem;

fs::path normalized_absolute(const fs::path& path) {
    std::error_code error;
    auto absolute = fs::absolute(path, error);
    if (error) {
        throw WorkspaceError("cannot resolve workspace root '" + path.string() +
                             "': " + error.message());
    }
    auto canonical = fs::weakly_canonical(absolute, error);
    return error ? absolute.lexically_normal() : canonical;
}

fs::path select_template(const fs::path& root, const fs::path& requested_template) {
    if (!requested_template.empty()) {
        return requested_template.is_absolute() ? requested_template : root / requested_template;
    }
    const auto preferred = root / "templates" / "solution.cpp";
    if (fs::is_regular_file(preferred)) {
        return preferred;
    }
    throw WorkspaceError("solution template not found; expected '" + preferred.string() + "'");
}

std::string read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw WorkspaceError("cannot read solution template '" + path.string() + "'");
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_new_file(const fs::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::out);
    if (!output) {
        throw WorkspaceError("cannot create solution '" + path.string() + "'");
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output) {
        throw WorkspaceError("cannot write solution '" + path.string() + "'");
    }
}

} // namespace

Workspace::Workspace(fs::path root) : root_(normalized_absolute(std::move(root))) {}

WorkspaceResult Workspace::create(const Problem& problem, const fs::path& template_path) const {
    Problem local(problem.contest_id(), problem.index(), root_);
    WorkspaceResult result{
        .solution = local.preferred_solution_path(),
        .solution_created = false,
    };

    const std::vector<fs::path> directories = {
        local.directory(),
        local.samples_path(),
        local.cases_path(),
        local.stress_path(),
    };
    for (const auto& directory : directories) {
        std::error_code error;
        fs::create_directories(directory, error);
        if (error) {
            throw WorkspaceError("cannot create directory '" + directory.string() +
                                 "': " + error.message());
        }
    }

    if (!fs::exists(result.solution)) {
        if (fs::is_regular_file(local.legacy_solution_path())) {
            write_new_file(result.solution, read_file(local.legacy_solution_path()));
        } else {
            const auto source_template = select_template(root_, template_path);
            if (!fs::is_regular_file(source_template)) {
                throw WorkspaceError("solution template is not a file: '" +
                                     source_template.string() + "'");
            }
            write_new_file(result.solution, read_file(source_template));
        }
        result.solution_created = true;
    } else if (!fs::is_regular_file(result.solution)) {
        throw WorkspaceError("solution path is not a file: '" + result.solution.string() + "'");
    }

    return result;
}

} // namespace cfprobs
