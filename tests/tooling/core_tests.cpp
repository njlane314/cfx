#include "cfprobs/bundle.hpp"
#include "cfprobs/problem.hpp"
#include "cfprobs/workspace.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

namespace fs = std::filesystem;

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("cfprobs-core-tests-" + std::to_string(stamp));
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
    } catch (const cfprobs::BundleError& error) {
        require(std::string(error.what()).find(text) != std::string::npos,
                "bundle error did not mention " + std::string(text));
        return;
    }
    throw std::runtime_error("expected BundleError");
}

template <class Function> void require_workspace_error(Function&& function, std::string_view text) {
    try {
        function();
    } catch (const cfprobs::WorkspaceError& error) {
        require(std::string(error.what()).find(text) != std::string::npos,
                "workspace error did not mention " + std::string(text));
        return;
    }
    throw std::runtime_error("expected WorkspaceError");
}

void test_problem_parsing() {
    TemporaryDirectory temporary;
    const auto& root = temporary.path();

    const auto canonical = cfprobs::Problem::parse("71A", root);
    require(canonical.id() == "71A", "canonical ID");
    require(canonical.legacy_id() == "A.71", "legacy ID");
    require(cfprobs::Problem::parse("a.71", root).id() == "71A", "legacy spelling");
    require(cfprobs::Problem::parse("a", "71", root).id() == "71A", "two-token spelling");
    require(cfprobs::Problem::parse("https://codeforces.com/problemset/problem/71/A", root).id() ==
                "71A",
            "problemset URL");
    require(cfprobs::Problem::parse("https://codeforces.com/contest/2227/problem/c?locale=en", root)
                    .id() == "2227C",
            "contest URL");
    require(cfprobs::Problem::parse("problems/cf/2227/B/solution.cpp", root).id() == "2227B",
            "new-layout path");
    require(cfprobs::Problem::parse("solutions/A.71.cpp", root).id() == "71A", "legacy path");
}

void test_problem_paths_and_fallback() {
    TemporaryDirectory temporary;
    const auto& root = temporary.path();
    const auto problem = cfprobs::Problem::parse("71A", root);

    require(problem.solution_path() == problem.preferred_solution_path(),
            "new path is preferred for a new problem");
    write(problem.legacy_solution_path(), "// legacy\n");
    require(problem.solution_path() == problem.legacy_solution_path(), "legacy solution fallback");
    fs::create_directories(problem.legacy_tests_path());
    require(problem.test_directories() == std::vector<fs::path>{problem.legacy_tests_path()},
            "legacy test fallback");
    write(problem.preferred_solution_path(), "// preferred\n");
    require(problem.solution_path() == problem.preferred_solution_path(),
            "preferred solution wins");
    require(problem.test_directories() ==
                (std::vector<fs::path>{problem.samples_path(), problem.cases_path(),
                                       problem.legacy_tests_path()}),
            "new-layout paths retain legacy regression cases");
}

void test_workspace_creation_is_idempotent() {
    TemporaryDirectory temporary;
    const auto& root = temporary.path();
    write(root / "templates" / "solution.cpp", "// template\n");

    const auto problem = cfprobs::Problem::parse("2227A", root);
    const cfprobs::Workspace workspace(root);
    const auto first = workspace.create(problem);
    require(first.solution_created, "solution created");
    require(read(first.solution) == "// template\n", "template copied");
    require(fs::is_directory(problem.samples_path()), "samples created");
    require(fs::is_directory(problem.cases_path()), "cases created");
    require(fs::is_directory(problem.stress_path()), "stress created");

    write(first.solution, "// keep me\n");
    const auto second = workspace.create(problem);
    require(!second.solution_created, "existing solution not recreated");
    require(read(second.solution) == "// keep me\n", "existing solution not overwritten");
}

void test_workspace_migrates_legacy_source_without_shadowing() {
    TemporaryDirectory temporary;
    const auto& root = temporary.path();
    write(root / "templates" / "solution.cpp", "// blank template\n");

    const auto problem = cfprobs::Problem::parse("71A", root);
    write(problem.legacy_solution_path(), "// keep legacy solution\n");
    write(problem.legacy_tests_path() / "case-1.in", "input\n");

    const auto result = cfprobs::Workspace(root).create(problem);
    require(result.solution_created, "preferred source created during migration");
    require(read(result.solution) == "// keep legacy solution\n",
            "legacy source copied instead of blank template");
    require(problem.solution_path() == problem.preferred_solution_path(),
            "migrated source activates new layout");
    require(problem.test_directories().back() == problem.legacy_tests_path(),
            "legacy regression cases remain visible after migration");
}

void test_current_problem_record() {
    TemporaryDirectory temporary;
    const auto& root = temporary.path();

    require(!cfprobs::current_problem(root), "current problem initially absent");

    const auto first = cfprobs::Problem::parse("2227A", root);
    cfprobs::remember_current_problem(first, root);
    require(read(root / ".build" / "current-problem") == "2227A\n",
            "current problem stored as a small canonical record");
    require(cfprobs::current_problem(root)->id() == "2227A", "current problem restored");

    const auto second = cfprobs::Problem::parse("71A", root);
    cfprobs::remember_current_problem(second, root);
    require(cfprobs::current_problem(root)->id() == "71A", "current problem replaced");

    write(root / ".build" / "current-problem", "not a problem\n");
    require_workspace_error([&] { static_cast<void>(cfprobs::current_problem(root)); },
                            "run probs PROBLEM again");

    write(root / ".build" / "current-problem", "71a\n");
    require_workspace_error([&] { static_cast<void>(cfprobs::current_problem(root)); },
                            "run probs PROBLEM again");

    write(root / ".build" / "current-problem", std::string(65, '1'));
    require_workspace_error([&] { static_cast<void>(cfprobs::current_problem(root)); },
                            "run probs PROBLEM again");
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

    const auto output = cfprobs::bundle(root / "main.cpp", root);
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
    require(cfprobs::bundle("source.cpp", root).find("constexpr int value = 4;") !=
                std::string::npos,
            "workspace include root");

    write(root / "missing.cpp", "#include \"not-there.hpp\"\n");
    require_bundle_error([&] { static_cast<void>(cfprobs::bundle("missing.cpp", root)); },
                         "not-there.hpp");

    write(root / "a.hpp", "#include \"b.hpp\"\n");
    write(root / "b.hpp", "#include \"a.hpp\"\n");
    require_bundle_error([&] { static_cast<void>(cfprobs::bundle("a.hpp", root)); }, "cycle");
}

} // namespace

int main() {
    try {
        test_problem_parsing();
        test_problem_paths_and_fallback();
        test_workspace_creation_is_idempotent();
        test_workspace_migrates_legacy_source_without_shadowing();
        test_current_problem_record();
        test_nested_and_repeated_bundling();
        test_include_root_and_errors();
    } catch (const std::exception& error) {
        std::cerr << "core_tests: " << error.what() << '\n';
        return 1;
    }
    std::cout << "core_tests: all tests passed\n";
}
