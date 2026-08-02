#include "cfx/cfx.hpp"
#include <tst.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;
using tst::check;

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("cfx-core-tests-" + std::to_string(stamp));
        fs::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    [[nodiscard]] const fs::path& path() const {
        return path_;
    }

  private:
    fs::path path_;
};

class ScopedEnvironment {
  public:
    ScopedEnvironment(std::string name, const std::string& value) : name_(std::move(name)) {
        if (const char* previous = std::getenv(name_.c_str()); previous != nullptr) {
            previous_ = previous;
        }
        if (::setenv(name_.c_str(), value.c_str(), 1) != 0) {
            throw std::runtime_error("test could not set " + name_);
        }
    }

    ~ScopedEnvironment() {
        if (previous_) {
            (void)::setenv(name_.c_str(), previous_->c_str(), 1);
        } else {
            (void)::unsetenv(name_.c_str());
        }
    }

  private:
    std::string name_;
    std::optional<std::string> previous_;
};

void write(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path);
    output << contents;
    if (!output) {
        throw std::runtime_error("test could not write " + path.string());
    }
}

std::string read(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

template <class Function> void require_bundle_error(Function&& function, std::string_view text) {
    try {
        function();
    } catch (const cfx::BundleError& error) {
        check(std::string(error.what()).find(text) != std::string::npos,
                "bundle error did not mention " + std::string(text));
        return;
    }
    throw std::runtime_error("expected BundleError");
}

template <class Function> void require_workspace_error(Function&& function, std::string_view text) {
    try {
        function();
    } catch (const cfx::WorkspaceError& error) {
        check(std::string(error.what()).find(text) != std::string::npos,
                "workspace error did not mention " + std::string(text));
        return;
    }
    throw std::runtime_error("expected WorkspaceError");
}

template <class Function> void require_problem_error(Function&& function) {
    try {
        function();
    } catch (const cfx::ProblemError&) {
        return;
    }
    throw std::runtime_error("expected ProblemError");
}

template <class Function> void require_runtime_error(Function&& function) {
    try {
        function();
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error("expected runtime_error");
}

void test_problem_parsing() {
    TemporaryDirectory temporary;
    const auto& root = temporary.path();

    const auto canonical = cfx::Problem::parse("71A", root);
    check(canonical.id() == "71A", "canonical ID");
    for (const std::string invalid : {
             "71a",
             "A.71",
             "https://codeforces.com/contest/71/problem/A",
             "codeforces/71/A",
         }) {
        require_problem_error([&] { static_cast<void>(cfx::Problem::parse(invalid, root)); });
    }
}

void test_problem_paths_and_inference() {
    TemporaryDirectory workspace;
    TemporaryDirectory outside;
    const auto& root = workspace.path();
    const auto problem = cfx::Problem::parse("71A", root);

    check(problem.solution_path() == problem.directory() / "solution.cpp",
            "canonical solution path");
    check(problem.metadata_path() == problem.directory() / "problem.json",
            "canonical metadata path");
    check(problem.samples_path() == problem.state_directory() / "samples",
            "fetched samples are external state");
    check(problem.test_directories() ==
                (std::vector<fs::path>{problem.samples_path(), problem.cases_path()}),
            "canonical test paths");

    fs::create_directories(problem.directory());
    const auto inferred = cfx::Problem::infer(problem.solution_path(), root);
    check(inferred && inferred->id() == "71A", "problem inferred inside root");

    const fs::path foreign = outside.path() / "codeforces" / "71" / "A";
    fs::create_directories(foreign);
    check(!cfx::Problem::infer(foreign, root), "absolute path outside root rejected");
    check(!cfx::Problem::infer(fs::relative(foreign, root), root),
            "relative path escaping root rejected");

    const fs::path link = root / "codeforces" / "72" / "A";
    fs::create_directories(link.parent_path());
    fs::create_directory_symlink(foreign, link);
    check(!cfx::Problem::infer(link, root), "symlink escaping root rejected");
}

void test_workspace_creation_is_idempotent() {
    TemporaryDirectory temporary;
    const auto& root = temporary.path();
    write(root / "solution.cpp", "// template\n");

    const auto problem = cfx::Problem::parse("2227A", root);
    const cfx::Workspace workspace(root);
    const auto first = workspace.create(problem);
    check(read(first) == "// template\n", "template copied");
    check(fs::is_directory(problem.samples_path()), "samples created");
    check(!fs::exists(root / ".cfx"), "runtime state stays outside archive");
    check(fs::is_directory(problem.cases_path()), "cases created");

    write(first, "// keep me\n");
    const auto second = workspace.create(problem);
    check(read(second) == "// keep me\n", "existing solution not overwritten");
}

void test_template_selection_and_archive_library() {
    TemporaryDirectory workspace;
    TemporaryDirectory configured;
    TemporaryDirectory library;
    const fs::path template_path = configured.path() / "solution.cpp";
    const ScopedEnvironment environment("CFX_SOLUTION_TEMPLATE", template_path.string());

    write(template_path, "// configured template\n");

    const auto problem = cfx::Problem::parse("71A", workspace.path());
    const auto created = cfx::Workspace(workspace.path()).create(problem);
    check(read(created) == "// configured template\n",
            "workspace uses configured solution template");

    write(workspace.path() / ".cfx" / "solution.cpp", "// archive template\n");
    const auto local_problem = cfx::Problem::parse("72A", workspace.path());
    const auto local_created = cfx::Workspace(workspace.path()).create(local_problem);
    check(read(local_created) == "// archive template\n",
            "archive template overrides configured template");

    write(workspace.path() / "solution.cpp",
          "#include <cp/value>\nint main() { return value; }\n");
    write(library.path() / "detail", "#pragma once\nconstexpr int detail = 8;\n");
    write(library.path() / "value",
          "#pragma once\n#include \"cp/detail\"\nconstexpr int value = detail;\n"
          "constexpr int header_source = 8;\n");
    fs::create_directories(workspace.path() / "include");
    fs::create_directory_symlink(library.path(), workspace.path() / "include" / "cp");
    const std::string bundled = cfx::bundle(workspace.path() / "solution.cpp", workspace.path());
    check(bundled.find("detail = 8") != std::string::npos &&
                bundled.find("header_source = 8") != std::string::npos,
            "bundle expands symlinked library headers transitively");
    check(bundled.find("#include <cp/value>") == std::string::npos,
            "bundle removes cp angle include");
    check(bundled.find("#include \"cp/detail\"") == std::string::npos,
            "bundle removes transitive quoted include");
}

void test_peek_submodule_bundling() {
    TemporaryDirectory temporary;
    const auto& root = temporary.path();
    write(root / "include" / "peek" / "peek.hpp",
          "#pragma once\n#include <array>\ninline constexpr int peek_answer = 42;\n");
    write(root / "solution.cpp", "#include <peek.hpp>\nint answer = peek_answer;\n");

    const std::string expected =
        "\n// ===== BEGIN peek.hpp =====\n"
        "#include <array>\n"
        "inline constexpr int peek_answer = 42;\n"
        "// ===== END peek.hpp =====\n\n"
        "int answer = peek_answer;\n";
    check(cfx::bundle(root / "solution.cpp", root) == expected,
            "bundle expands include/peek/peek.hpp exactly");
}

void test_file_operations() {
    TemporaryDirectory temporary;
    const auto& root = temporary.path();
    const fs::path path = root / "data.bin";
    const std::string binary{"alpha\0beta", 10};

    cfx::write_text(path, binary);
    check(cfx::read_text(path) == binary, "binary-safe text round trip");

    const auto timestamp = fs::last_write_time(path);
    cfx::write_atomic(path, binary);
    check(fs::last_write_time(path) == timestamp, "identical atomic write is a no-op");

    cfx::write_atomic(path, "replacement");
    check(cfx::read_text(path) == "replacement", "atomic replacement");

    const fs::path directory = root / "directory";
    fs::create_directory(directory);
    require_runtime_error([&] { cfx::write_atomic(directory, "not a directory"); });
    check(!fs::exists(directory.string() + ".tmp." + std::to_string(::getpid())),
            "failed atomic write removes its temporary");
}

void test_current_problem_record() {
    TemporaryDirectory temporary;
    const auto& root = temporary.path();

    check(!cfx::current_problem(root), "current problem initially absent");

    const auto first = cfx::Problem::parse("2227A", root);
    cfx::remember_current_problem(first, root);
    const fs::path current = cfx::state_root(root) / "current-problem";
    check(read(current) == "2227A\n",
            "current problem stored as a small canonical record");
    check(cfx::current_problem(root)->id() == "2227A", "current problem restored");

    const auto second = cfx::Problem::parse("71A", root);
    cfx::remember_current_problem(second, root);
    check(cfx::current_problem(root)->id() == "71A", "current problem replaced");

    fs::create_directories(root / ".build");
    write(root / ".build" / "disposable", "cache\n");
    fs::remove_all(root / ".build");
    check(cfx::current_problem(root)->id() == "71A", "clean preserves current problem");

    write(current, "not a problem\n");
    require_workspace_error([&] { static_cast<void>(cfx::current_problem(root)); },
                            "run cfx PROBLEM again");

    write(current, "71a\n");
    require_workspace_error([&] { static_cast<void>(cfx::current_problem(root)); },
                            "run cfx PROBLEM again");

    write(current, std::string(65, '1'));
    require_workspace_error([&] { static_cast<void>(cfx::current_problem(root)); },
                            "run cfx PROBLEM again");
}

void test_nested_and_repeated_bundling() {
    TemporaryDirectory temporary;
    const auto& root = temporary.path();
    write(root / "main.cpp", "#include <vector>\n"
                             "#include \"parts/first.hpp\"\n"
                             "#include \"sibling.hpp\"\n"
                             "#include <cp/shared>\n"
                             "int main() { return first + sibling + shared; }\n");
    write(root / "parts" / "first.hpp", "#pragma once\n"
                                        "#include \"nested/value.hpp\"\n"
                                        "#include \"cp/shared\"\n"
                                        "constexpr int first = nested;\n");
    write(root / "parts" / "nested" / "value.hpp", "#pragma once\nconstexpr int nested = 1;\n");
    write(root / "sibling.hpp", "#pragma once\nconstexpr int sibling = 2;\n");
    write(root / "include" / "cp" / "shared", "#pragma once\nconstexpr int shared = 3;\n");

    const auto output = cfx::bundle(root / "main.cpp", root);
    check(output.find("#include <vector>") != std::string::npos, "system include preserved");
    check(output.find("constexpr int nested = 1;") != std::string::npos,
            "nested include expanded");
    check(output.find("constexpr int sibling = 2;") != std::string::npos,
            "sibling resolves relative to its own caller");
    const auto shared = output.find("constexpr int shared = 3;");
    check(shared != std::string::npos, "shared include expanded");
    check(output.find("constexpr int shared = 3;", shared + 1) == std::string::npos,
            "repeated include suppressed");
    check(output.find("#pragma once") == std::string::npos, "pragma once removed");
}

void test_include_root_and_errors() {
    TemporaryDirectory temporary;
    const auto& root = temporary.path();
    write(root / "include" / "cp" / "value", "#pragma once\nconstexpr int value = 4;\n");
    write(root / "source.cpp", "#include <cp/value>\nint answer = value;\n");
    check(cfx::bundle("source.cpp", root).find("constexpr int value = 4;") != std::string::npos,
            "workspace include root");

    write(root / "missing.cpp", "#include \"not-there.hpp\"\n");
    require_bundle_error([&] { static_cast<void>(cfx::bundle("missing.cpp", root)); },
                         "not-there.hpp");

    write(root / "include" / "cp" / "a", "#include \"cp/b\"\n");
    write(root / "include" / "cp" / "b", "#include <cp/a>\n");
    write(root / "cycle.cpp", "#include <cp/a>\n");
    require_bundle_error([&] { static_cast<void>(cfx::bundle("cycle.cpp", root)); }, "cycle");
}

} // namespace

int main() {
    return tst::run("core tests", [] {
        test_problem_parsing();
        test_problem_paths_and_inference();
        test_workspace_creation_is_idempotent();
        test_template_selection_and_archive_library();
        test_peek_submodule_bundling();
        test_file_operations();
        test_current_problem_record();
        test_nested_and_repeated_bundling();
        test_include_root_and_errors();
    });
}
