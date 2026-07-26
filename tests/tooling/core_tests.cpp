#include "cfx/assets.hpp"
#include "cfx/bundle.hpp"
#include "cfx/file.hpp"
#include "cfx/problem.hpp"
#include "cfx/runtime.hpp"
#include "cfx/workspace.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

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

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class Function> void require_bundle_error(Function&& function, std::string_view text) {
    try {
        function();
    } catch (const cfx::BundleError& error) {
        require(std::string(error.what()).find(text) != std::string::npos,
                "bundle error did not mention " + std::string(text));
        return;
    }
    throw std::runtime_error("expected BundleError");
}

template <class Function> void require_workspace_error(Function&& function, std::string_view text) {
    try {
        function();
    } catch (const cfx::WorkspaceError& error) {
        require(std::string(error.what()).find(text) != std::string::npos,
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
    require(canonical.id() == "71A", "canonical ID");
    require(cfx::Problem::parse("a.71", root).id() == "71A", "alternate spelling");
    require(cfx::Problem::parse("a", "71", root).id() == "71A", "two-token spelling");
    require(cfx::Problem::parse("https://codeforces.com/problemset/problem/71/A", root).id() ==
                "71A",
            "problemset URL");
    require(
        cfx::Problem::parse("https://codeforces.com/contest/2227/problem/c?locale=en", root).id() ==
            "2227C",
        "contest URL");
    require(cfx::Problem::parse("codeforces/2227/B/solution.cpp", root).id() == "2227B",
            "workspace path");
    require_problem_error(
        [&] { static_cast<void>(cfx::Problem::parse("solutions/A.71.cpp", root)); });
}

void test_problem_paths_and_inference() {
    TemporaryDirectory workspace;
    TemporaryDirectory outside;
    const auto& root = workspace.path();
    const auto problem = cfx::Problem::parse("71A", root);

    require(problem.solution_path() == problem.directory() / "solution.cpp",
            "canonical solution path");
    require(problem.metadata_path() == problem.directory() / "problem.json",
            "canonical metadata path");
    require(problem.samples_path() == problem.state_directory() / "samples",
            "fetched samples are external state");
    require(problem.test_directories() ==
                (std::vector<fs::path>{problem.samples_path(), problem.cases_path()}),
            "canonical test paths");

    fs::create_directories(problem.directory());
    const auto inferred = cfx::Problem::infer(problem.solution_path(), root);
    require(inferred && inferred->id() == "71A", "problem inferred inside root");

    const fs::path foreign = outside.path() / "codeforces" / "71" / "A";
    fs::create_directories(foreign);
    require(!cfx::Problem::infer(foreign, root), "absolute path outside root rejected");
    require(!cfx::Problem::infer(fs::relative(foreign, root), root),
            "relative path escaping root rejected");

    const fs::path link = root / "codeforces" / "72" / "A";
    fs::create_directories(link.parent_path());
    fs::create_directory_symlink(foreign, link);
    require(!cfx::Problem::infer(link, root), "symlink escaping root rejected");
}

void test_workspace_creation_is_idempotent() {
    TemporaryDirectory temporary;
    const auto& root = temporary.path();
    write(root / "templates" / "solution.cpp", "// template\n");

    const auto problem = cfx::Problem::parse("2227A", root);
    const cfx::Workspace workspace(root);
    const auto first = workspace.create(problem);
    require(first.solution_created, "solution created");
    require(read(first.solution) == "// template\n", "template copied");
    require(fs::is_directory(problem.samples_path()), "samples created");
    require(!fs::exists(root / ".cfx"), "runtime state stays outside archive");
    require(fs::is_directory(problem.cases_path()), "cases created");
    require(fs::is_directory(problem.stress_path()), "stress created");

    write(first.solution, "// keep me\n");
    const auto second = workspace.create(problem);
    require(!second.solution_created, "existing solution not recreated");
    require(read(second.solution) == "// keep me\n", "existing solution not overwritten");
}

void test_separate_asset_root() {
    TemporaryDirectory workspace;
    TemporaryDirectory assets;
    const ScopedEnvironment environment("CFX_ASSET_ROOT", assets.path().string());

    write(assets.path() / "templates" / "solution.cpp", "// installed template\n");
    write(assets.path() / "include" / "cp" / "value.hpp",
          "#pragma once\nconstexpr int value = 7;\n");

    const auto problem = cfx::Problem::parse("71A", workspace.path());
    const auto created = cfx::Workspace(workspace.path()).create(problem);
    require(read(created.solution) == "// installed template\n",
            "workspace uses template from separate asset root");
    require(cfx::asset_root(workspace.path()) == fs::weakly_canonical(assets.path()),
            "configured asset root resolves independently");

    write(workspace.path() / ".cfx" / "solution.cpp", "// archive template\n");
    const auto local_problem = cfx::Problem::parse("72A", workspace.path());
    const auto local_created = cfx::Workspace(workspace.path()).create(local_problem);
    require(read(local_created.solution) == "// archive template\n",
            "archive template overrides installed template");

    write(workspace.path() / "explicit.cpp", "// explicit template\n");
    const auto explicit_problem = cfx::Problem::parse("73A", workspace.path());
    const auto explicit_created =
        cfx::Workspace(workspace.path()).create(explicit_problem, "explicit.cpp");
    require(read(explicit_created.solution) == "// explicit template\n",
            "explicit template overrides archive template");

    write(workspace.path() / "solution.cpp",
          "#include \"cp/value.hpp\"\nint main() { return value; }\n");
    const std::string installed = cfx::bundle(workspace.path() / "solution.cpp", workspace.path());
    require(installed.find("value = 7") != std::string::npos,
            "bundle resolves installed library header");

    write(workspace.path() / "include" / "cp" / "value.hpp",
          "#pragma once\nconstexpr int value = 8;\n");
    const std::string local = cfx::bundle(workspace.path() / "solution.cpp", workspace.path());
    require(local.find("value = 8") != std::string::npos,
            "workspace library header overrides installed asset");
}

void test_file_operations() {
    TemporaryDirectory temporary;
    const auto& root = temporary.path();
    const fs::path path = root / "data.bin";
    const std::string binary{"alpha\0beta", 10};

    cfx::write_text(path, binary);
    require(cfx::read_text(path) == binary, "binary-safe text round trip");

    const auto timestamp = fs::last_write_time(path);
    cfx::write_atomic(path, binary);
    require(fs::last_write_time(path) == timestamp, "identical atomic write is a no-op");

    cfx::write_atomic(path, "replacement");
    require(cfx::read_text(path) == "replacement", "atomic replacement");

    const fs::path directory = root / "directory";
    fs::create_directory(directory);
    require_runtime_error([&] { cfx::write_atomic(directory, "not a directory"); });
    require(!fs::exists(directory.string() + ".tmp." + std::to_string(::getpid())),
            "failed atomic write removes its temporary");
}

void test_current_problem_record() {
    TemporaryDirectory temporary;
    const auto& root = temporary.path();

    require(!cfx::current_problem(root), "current problem initially absent");

    const auto first = cfx::Problem::parse("2227A", root);
    cfx::remember_current_problem(first, root);
    const fs::path current = cfx::state_root(root) / "current-problem";
    require(read(current) == "2227A\n",
            "current problem stored as a small canonical record");
    require(cfx::current_problem(root)->id() == "2227A", "current problem restored");

    const auto second = cfx::Problem::parse("71A", root);
    cfx::remember_current_problem(second, root);
    require(cfx::current_problem(root)->id() == "71A", "current problem replaced");

    fs::create_directories(root / ".build");
    write(root / ".build" / "disposable", "cache\n");
    fs::remove_all(root / ".build");
    require(cfx::current_problem(root)->id() == "71A", "clean preserves current problem");

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
                             "#include \"shared.hpp\"\n"
                             "int main() { return first + sibling + shared; }\n");
    write(root / "parts" / "first.hpp", "#pragma once\n"
                                        "#include \"nested/value.hpp\"\n"
                                        "#include \"../shared.hpp\"\n"
                                        "constexpr int first = nested;\n");
    write(root / "parts" / "nested" / "value.hpp", "#pragma once\nconstexpr int nested = 1;\n");
    write(root / "sibling.hpp", "#pragma once\nconstexpr int sibling = 2;\n");
    write(root / "shared.hpp", "#pragma once\nconstexpr int shared = 3;\n");

    const auto output = cfx::bundle(root / "main.cpp", root);
    require(output.find("#include <vector>") != std::string::npos, "system include preserved");
    require(output.find("constexpr int nested = 1;") != std::string::npos,
            "nested include expanded");
    require(output.find("constexpr int sibling = 2;") != std::string::npos,
            "sibling resolves relative to its own caller");
    const auto shared = output.find("constexpr int shared = 3;");
    require(shared != std::string::npos, "shared include expanded");
    require(output.find("constexpr int shared = 3;", shared + 1) == std::string::npos,
            "repeated include suppressed");
    require(output.find("#pragma once") == std::string::npos, "pragma once removed");
}

void test_include_root_and_errors() {
    TemporaryDirectory temporary;
    const auto& root = temporary.path();
    write(root / "include" / "cp" / "value.hpp", "#pragma once\nconstexpr int value = 4;\n");
    write(root / "source.cpp", "#include \"cp/value.hpp\"\nint answer = value;\n");
    require(cfx::bundle("source.cpp", root).find("constexpr int value = 4;") != std::string::npos,
            "workspace include root");

    write(root / "missing.cpp", "#include \"not-there.hpp\"\n");
    require_bundle_error([&] { static_cast<void>(cfx::bundle("missing.cpp", root)); },
                         "not-there.hpp");

    write(root / "a.hpp", "#include \"b.hpp\"\n");
    write(root / "b.hpp", "#include \"a.hpp\"\n");
    require_bundle_error([&] { static_cast<void>(cfx::bundle("a.hpp", root)); }, "cycle");
}

} // namespace

int main() {
    try {
        test_problem_parsing();
        test_problem_paths_and_inference();
        test_workspace_creation_is_idempotent();
        test_separate_asset_root();
        test_file_operations();
        test_current_problem_record();
        test_nested_and_repeated_bundling();
        test_include_root_and_errors();
    } catch (const std::exception& error) {
        std::cerr << "core_tests: " << error.what() << '\n';
        return 1;
    }
    std::cout << "core_tests: all tests passed\n";
}
