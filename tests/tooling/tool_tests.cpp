#include "cfprobs/browser.hpp"
#include "cfprobs/codeforces.hpp"
#include "cfprobs/companion.hpp"
#include "cfprobs/compiler.hpp"
#include "cfprobs/hash.hpp"
#include "cfprobs/json.hpp"
#include "cfprobs/judge.hpp"
#include "cfprobs/process.hpp"
#include "cfprobs/submission.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

class TemporaryDirectory {
  public:
    TemporaryDirectory()
        : path_(fs::temp_directory_path() / ("cfprobs-tool-tests-" + std::to_string(::getpid()))) {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
        fs::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    const fs::path& path() const {
        return path_;
    }

  private:
    fs::path path_;
};

void write(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output || !(output << value)) {
        throw std::runtime_error("cannot write test file");
    }
}

std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(),
    };
}

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class Action> void check_throws(Action action, const std::string& message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

void test_json_and_codeforces() {
    const cfprobs::Json value =
        cfprobs::parse_json(R"({"text":"line\nnext","values":[true,null,2.5]})");
    check(value.at("text").string() == "line\nnext", "JSON string decoding");
    check(value.at("values").array().size() == 3, "JSON array decoding");
    check(cfprobs::json_quote("a\n\"b") == "\"a\\n\\\"b\"", "JSON quoting");

    const std::vector<std::string> indexes =
        cfprobs::parse_contest_indexes(R"({"status":"OK","result":{"problems":[)"
                                       R"({"index":"A"},{"index":"B1"},{"index":"A"}]}})");
    check(indexes == std::vector<std::string>({"A", "B1"}),
          "contest indexes preserve order and remove duplicates");
    check_throws(
        [] {
            (void)cfprobs::parse_contest_indexes(R"({"status":"FAILED","comment":"bad contest"})");
        },
        "failed API responses must throw");
}

void test_process_and_normalization(const fs::path& root) {
    check(cfprobs::split_command_words(R"(one "two three" 'four five')") ==
              std::vector<std::string>({"one", "two three", "four five"}),
          "command word splitting");
    check(cfprobs::normalize_output("one  \n\n two\t\n") == "one\n two", "output normalization");

    const cfprobs::ProcessResult timeout =
        cfprobs::run_process({"/bin/sh", "-c", "sleep 1"}, cfprobs::ProcessOptions{
                                                               std::nullopt,
                                                               root / "timeout.out",
                                                               root / "timeout.err",
                                                               std::chrono::milliseconds(20),
                                                               std::nullopt,
                                                           });
    check(timeout.timed_out && timeout.status == 124, "process timeout");

    const auto detached_started = std::chrono::steady_clock::now();
    cfprobs::launch_detached_process({"/bin/sh", "-c", "sleep 1"});
    const auto detached_elapsed = std::chrono::steady_clock::now() - detached_started;
    check(detached_elapsed < std::chrono::milliseconds(500),
          "detached process returns without waiting for completion");
    check_throws(
        [] { cfprobs::launch_detached_process({"/definitely/missing/cfprobs-command"}); },
        "detached process reports exec failure");
}

void test_build_cache(const fs::path& root) {
    write(root / "include" / "value.hpp", "#pragma once\n#define VALUE 7\n");
    write(root / "solution.cpp", "#include <value.hpp>\n"
                                 "int main() { return VALUE == 7 ? 0 : 1; }\n");
    const cfprobs::Builder builder(root);
    const cfprobs::BuildResult first = builder.build_source(root / "solution.cpp", "cache-test");
    check(first.compiled, "first cache build compiles");
    const cfprobs::BuildResult second = builder.build_source(root / "solution.cpp", "cache-test");
    check(!second.compiled, "unchanged build uses cache");
    check(first.digest == second.digest, "unchanged digest is stable");

    write(root / "include" / "value.hpp", "#pragma once\n#define VALUE 8\n");
    const cfprobs::BuildResult third = builder.build_source(root / "solution.cpp", "cache-test");
    check(third.compiled, "included-header edit rebuilds");
    check(third.digest != first.digest, "included-header edit changes digest");

    write(root / "submission.cpp",
          "#ifdef LOCAL\n#error submission build must not define LOCAL\n#endif\n"
          "int main() {}\n");
    const cfprobs::BuildResult submission = builder.build_source(
        root / "submission.cpp", "submission-mode", cfprobs::BuildOptions{true, false, false});
    check(submission.compiled, "submission mode compiles without LOCAL");
}

void test_companion_import(const fs::path& root) {
    write(root / "templates" / "solution.cpp", "int main() {}\n");
    const std::string payload = R"({"name":"Way Too Long Words",)"
                                R"("url":"https://codeforces.com/contest/71/problem/A",)"
                                R"("timeLimit":1000,"memoryLimit":256,)"
                                R"("tests":[{"input":"1\nword\n","output":"word\n"}]})";
    const cfprobs::CompanionPackage package = cfprobs::parse_companion_package(payload, root);
    check(package.problem.id() == "71A", "Companion URL parsing");
    const cfprobs::ImportResult first = cfprobs::import_companion_package(package, root);
    check(first.files_written == 2, "first Companion import writes pair");
    check(read(package.problem.samples_path() / "01.in") == "1\nword\n", "Companion input content");
    const cfprobs::ImportResult second = cfprobs::import_companion_package(package, root);
    check(second.files_written == 0, "Companion import is idempotent");

    write(package.problem.samples_path() / "02.in", "stale\n");
    write(package.problem.samples_path() / "02.out", "stale\n");
    check_throws([&] { (void)cfprobs::import_companion_package(package, root); },
                 "shorter sample refresh requires force");
    check(fs::is_regular_file(package.problem.samples_path() / "02.in"),
          "rejected refresh preserves stale pair");
    const cfprobs::ImportResult shortened = cfprobs::import_companion_package(package, root, true);
    check(shortened.files_written == 2, "forced refresh replaces complete set");
    check(!fs::exists(package.problem.samples_path() / "02.in") &&
              !fs::exists(package.problem.samples_path() / "02.out"),
          "forced refresh removes stale pairs");

    cfprobs::CompanionPackage changed = package;
    changed.samples.front().input = "different\n";
    check_throws([&] { (void)cfprobs::import_companion_package(changed, root); },
                 "differing sample pair requires force");
    check(read(package.problem.samples_path() / "01.in") == "1\nword\n",
          "failed pair update preserves existing input");
    const cfprobs::ImportResult forced = cfprobs::import_companion_package(changed, root, true);
    check(forced.files_written == 2, "forced pair refresh");
    check(read(package.problem.samples_path() / "01.in") == "different\n", "forced input content");
}

void test_submission_preparation(const fs::path& root) {
    const cfprobs::Problem problem("88", "A", root);
    write(problem.preferred_solution_path(),
          "#include <iostream>\n"
          "#ifdef LOCAL\n"
          "constexpr bool local_build = true;\n"
          "#else\n"
          "constexpr bool local_build = false;\n"
          "#endif\n"
          "int main() { int value = 0; std::cin >> value; "
          "std::cout << value + (local_build ? 0 : 0) << '\\n'; }\n");
    write(problem.samples_path() / "01.in", "7\n");
    write(problem.samples_path() / "01.out", "7\n");

    const cfprobs::SubmissionArtifact artifact = cfprobs::prepare_submission(root, problem);
    check(fs::is_regular_file(artifact.source), "submission artifact exists");
    check(artifact.source_text == read(artifact.source), "submission carries the pinned source");
    check(artifact.target == "88A", "submission target");
    check(artifact.language == "GNU C++20", "submission language");
    check(artifact.page_url == "https://codeforces.com/problemset/submit",
          "submission uses the archive problemset page");
    check(artifact.source_hash.size() == 32, "submission hash");
    check(artifact.source_hash == cfprobs::content_digest(artifact.source_text),
          "submission hash matches pinned source");
    check(artifact.source.filename().string().find(artifact.source_hash.substr(0, 16)) !=
              std::string::npos,
          "submission artifact name contains hash");

    const cfprobs::Problem untested("88", "B", root);
    write(untested.preferred_solution_path(), "int main() {}\n");
    check_throws([&] { (void)cfprobs::prepare_submission(root, untested); },
                 "submission requires at least one complete test pair");
}

void test_browser_submission_states() {
    if (std::getenv("CFPROBS_TEST_BROWSER_LOG") == nullptr ||
        std::getenv("CFPROBS_TEST_SUBMISSION_PAYLOAD") == nullptr) {
        return;
    }

    cfprobs::BrowserBridgeOptions options;
    options.request_timeout = std::chrono::milliseconds(500);
    options.connect_timeout = std::chrono::milliseconds(1000);
    options.wait_timeout = std::chrono::milliseconds(1500);
    options.extension_id = "abcdefghijklmnopabcdefghijklmnop";
    const cfprobs::BrowserSubmitRequest request{
        "https://codeforces.com/problemset/submit",
        "99993C",
        "C",
        "GNU C++20",
        "int main() {}\n",
    };

    ::setenv("CFPROBS_TEST_CONNECTOR_MODE", "ready-only", 1);
    bool unavailable = false;
    try {
        (void)cfprobs::submit_in_browser(request, options);
    } catch (const cfprobs::BrowserConnectorUnavailable&) {
        unavailable = true;
    }
    check(unavailable, "ready without a source request is safe to fall back");

    ::setenv("CFPROBS_TEST_CONNECTOR_MODE", "pre-submit-error", 1);
    bool sign_in_required = false;
    try {
        (void)cfprobs::submit_in_browser(request, options);
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        sign_in_required = message.find("Codeforces submission failed") != std::string::npos &&
                           message.find("not signed in") != std::string::npos;
    }
    check(sign_in_required, "known sign-in failure is reported before source is requested");

    ::setenv("CFPROBS_TEST_CONNECTOR_MODE", "source-only", 1);
    bool source_unknown = false;
    try {
        (void)cfprobs::submit_in_browser(request, options);
    } catch (const cfprobs::BrowserConnectorUnavailable&) {
        check(false, "served source must never trigger automatic fallback");
    } catch (const std::runtime_error& error) {
        source_unknown = std::string(error.what()).find("status is unknown") != std::string::npos;
    }
    check(source_unknown, "missing result after serving source has unknown status");

    ::setenv("CFPROBS_TEST_CONNECTOR_MODE", "unknown-result", 1);
    bool reported_unknown = false;
    try {
        (void)cfprobs::submit_in_browser(request, options);
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        reported_unknown = message.find("submission status is unknown") != std::string::npos &&
                           message.find("Codeforces submission failed") == std::string::npos;
    }
    ::unsetenv("CFPROBS_TEST_CONNECTOR_MODE");
    check(reported_unknown, "uncertain browser result is not reported as a safe rejection");
}

} // namespace

int main() {
    try {
        TemporaryDirectory temporary;
        test_json_and_codeforces();
        test_process_and_normalization(temporary.path());
        test_build_cache(temporary.path() / "build");
        test_companion_import(temporary.path() / "companion");
        test_submission_preparation(temporary.path() / "submission");
        test_browser_submission_states();
        std::cout << "tool integration tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "tool test failed: " << error.what() << '\n';
        return 1;
    }
}
