#include "cfx/workspace.hpp"

#include "cfx/assets.hpp"
#include "cfx/file.hpp"
#include "cfx/runtime.hpp"

#include <system_error>
#include <utility>

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
    const auto preferred = asset_root(root) / "templates" / "solution.cpp";
    if (fs::is_regular_file(preferred)) {
        return preferred;
    }
    throw WorkspaceError("solution template not found; expected '" + preferred.string() + "'");
}

fs::path current_problem_path(const fs::path& root) {
    const fs::path archive = normalized_absolute(root);
    return cfx::state_root(archive) / "current-problem";
}

} // namespace

Workspace::Workspace(fs::path root) : root_(normalized_absolute(std::move(root))) {}

WorkspaceResult Workspace::create(const Problem& problem, const fs::path& template_path) const {
    Problem local(problem.contest_id(), problem.index(), root_);
    WorkspaceResult result{
        .solution = local.solution_path(),
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
        const auto source_template = select_template(root_, template_path);
        if (!fs::is_regular_file(source_template)) {
            throw WorkspaceError("solution template is not a file: '" + source_template.string() +
                                 "'");
        }
        try {
            write_text(result.solution, read_text(source_template));
        } catch (const std::exception& error) {
            throw WorkspaceError(error.what());
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

} // namespace cfx
