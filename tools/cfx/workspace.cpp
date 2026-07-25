#include "cfx/workspace.hpp"

#include <array>
#include <fstream>
#include <system_error>
#include <utility>
#include <unistd.h>

namespace cfx {
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

fs::path current_problem_path(const fs::path& root) {
    return normalized_absolute(root) / ".build" / "current-problem";
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

void remember_current_problem(const Problem& problem, const fs::path& root) {
    const fs::path path = current_problem_path(root);
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error) {
        throw WorkspaceError("cannot create current problem directory '" +
                             path.parent_path().string() + "': " + error.message());
    }

    const fs::path temporary = path.string() + ".tmp." + std::to_string(::getpid());
    bool written = false;
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (output) {
            output << problem.id() << '\n';
            output.close();
            written = static_cast<bool>(output);
        }
    }
    if (!written) {
        fs::remove(temporary, error);
        throw WorkspaceError("cannot write current problem record '" + path.string() + "'");
    }
    fs::rename(temporary, path, error);
    if (error) {
        fs::remove(temporary, error);
        throw WorkspaceError("cannot replace current problem record '" + path.string() + "'");
    }
}

std::optional<Problem> current_problem(const fs::path& root) {
    const fs::path path = current_problem_path(root);
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::error_code error;
        const bool exists = fs::exists(path, error);
        if (error) {
            throw WorkspaceError("cannot inspect current problem record '" + path.string() +
                                 "': " + error.message());
        }
        if (!exists) {
            return std::nullopt;
        }
        throw WorkspaceError("cannot read current problem record '" + path.string() + "'");
    }

    std::array<char, 65> buffer{};
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto length = static_cast<std::size_t>(input.gcount());
    if (length == buffer.size()) {
        throw WorkspaceError("invalid current problem record '" + path.string() +
                             "'; run cfx PROBLEM again");
    }

    std::string contents(buffer.data(), length);
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

} // namespace cfx
