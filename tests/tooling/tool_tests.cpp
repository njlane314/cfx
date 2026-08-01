#include "cfx/browser.hpp"
#include "cfx/browser_http.hpp"
#include "cfx/codeforces.hpp"
#include "cfx/companion.hpp"
#include "cfx/compiler.hpp"
#include "cfx/hash.hpp"
#include "cfx/json.hpp"
#include "cfx/judge.hpp"
#include "cfx/process.hpp"
#include "cfx/submission.hpp"

#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

class TemporaryDirectory {
  public:
    TemporaryDirectory()
        : path_(fs::temp_directory_path() / ("cfx-tool-tests-" + std::to_string(::getpid()))) {
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

void check(bool condition, const std::string& message);

class SubmissionApiServer {
  public:
    explicit SubmissionApiServer(std::vector<std::string> responses,
                                 std::vector<int> expected_from = {})
        : responses_(std::move(responses)), expected_from_(std::move(expected_from)),
          worker_([this] { run(); }) {}

    ~SubmissionApiServer() {
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    [[nodiscard]] std::string base_url() const {
        return "http://127.0.0.1:" + std::to_string(server_.port());
    }

    void wait() {
        worker_.join();
        if (error_) {
            std::rethrow_exception(error_);
        }
    }

  private:
    void run() {
        try {
            check(expected_from_.empty() || expected_from_.size() == responses_.size(),
                  "Codeforces API fixture has mismatched page expectations");
            for (std::size_t index = 0; index < responses_.size(); ++index) {
                const std::string& body = responses_[index];
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                std::optional<cfx::browser_http::Connection> accepted =
                    server_.accept_until(deadline, std::chrono::seconds(1));
                check(accepted.has_value(), "Codeforces API fixture timed out");
                cfx::browser_http::Connection connection = std::move(*accepted);
                const cfx::browser_http::Request request = connection.read(
                    deadline, [this, index](const cfx::browser_http::RequestHead& head) {
                        check(head.method == "GET", "Codeforces API fixture expected GET");
                        check(head.target.starts_with("/contest.status?contestId=99993&handle=panicsort&"),
                              "Codeforces API fixture received the wrong query");
                        if (!expected_from_.empty()) {
                            const std::string page =
                                "&from=" + std::to_string(expected_from_[index]) + "&count=50";
                            check(head.target.find(page) != std::string::npos,
                                  "Codeforces API fixture received the wrong page");
                        }
                        check(head.content_length.value_or(0) == 0,
                              "Codeforces API fixture received a request body");
                        return std::size_t{0};
                    });
                (void)request;
                connection.send(cfx::browser_http::Response{
                    200,
                    {{"Content-Type", "application/json"}},
                    body,
                });
            }
        } catch (...) {
            error_ = std::current_exception();
        }
    }

    cfx::browser_http::Server server_;
    std::vector<std::string> responses_;
    std::vector<int> expected_from_;
    std::thread worker_;
    std::exception_ptr error_;
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
    const cfx::Json value =
        cfx::parse_json(R"({"text":"line\nnext","values":[true,null,2.5]})");
    check(value.at("text").string() == "line\nnext", "JSON string decoding");
    check(value.at("values").array().size() == 3, "JSON array decoding");
    check(cfx::json_quote("a\n\"b") == "\"a\\n\\\"b\"", "JSON quoting");
}

void test_codeforces_poll_interval() {
    SubmissionApiServer api({
        R"({"status":"OK","result":[{"id":123456789,"contestId":99993,)"
        R"("problem":{"contestId":99993,"index":"C"},)"
        R"("author":{"members":[{"handle":"panicsort"}],"participantType":"CONTESTANT"},)"
        R"("verdict":"OK",)"
        R"("testset":"PRETESTS","passedTestCount":20,"timeConsumedMillis":46,)"
        R"("memoryConsumedBytes":102400}]})",
        R"({"status":"OK","result":[{"id":123456789,"contestId":99993,)"
        R"("problem":{"contestId":99993,"index":"C"},)"
        R"("author":{"members":[{"handle":"someone_else"}],"participantType":"CONTESTANT"},)"
        R"("verdict":"TESTING"}]})",
    });
    ::setenv("CFX_API_BASE", api.base_url().c_str(), 1);
    const auto started = std::chrono::steady_clock::now();
    const auto status = cfx::poll_submission_status(
        "99993", "C", "panicsort", "123456789", started + std::chrono::seconds(1),
        std::chrono::milliseconds(25));
    const auto elapsed = std::chrono::steady_clock::now() - started;
    check_throws(
        [] {
            (void)cfx::poll_submission_status(
                "99993", "C", "panicsort", "123456789",
                std::chrono::steady_clock::now() + std::chrono::seconds(1),
                std::chrono::milliseconds(1));
        },
        "official API polling rejects a submission from the wrong author");
    api.wait();
    ::unsetenv("CFX_API_BASE");
    check(status.terminal && status.verdict_text == "Accepted (pretests)" &&
              status.participant_type == "CONTESTANT" && status.testset == "PRETESTS" &&
              status.passed_test_count == 20 && status.time_consumed_millis == 46 &&
              status.memory_consumed_bytes == 102400 &&
              elapsed >= std::chrono::milliseconds(25),
          "official API polling returns complete provisional results after the initial interval");
}

void test_codeforces_status_pagination() {
    std::string first_page = R"({"status":"OK","result":[)";
    for (int offset = 0; offset < 50; ++offset) {
        if (offset != 0) first_page += ',';
        first_page += "{\"id\":" + std::to_string(123456900 - offset) + '}';
    }
    first_page += "]}";

    SubmissionApiServer api({
        first_page,
        R"({"status":"OK","result":[{"id":123456800,"contestId":99993,)"
        R"("problem":{"contestId":99993,"index":"C"},)"
        R"("author":{"members":[{"handle":"panicsort"}],"participantType":"CONTESTANT"},)"
        R"("verdict":"OK","testset":"TESTS","passedTestCount":20,)"
        R"("timeConsumedMillis":46,"memoryConsumedBytes":102400}]})",
    }, {1, 51});
    ::setenv("CFX_API_BASE", api.base_url().c_str(), 1);
    const cfx::CodeforcesSubmission status = cfx::poll_submission_status(
        "99993", "C", "panicsort", "123456800",
        std::chrono::steady_clock::now() + std::chrono::seconds(5),
        std::chrono::milliseconds(1));
    api.wait();
    ::unsetenv("CFX_API_BASE");
    check(status.found && status.terminal && status.verdict == "OK" &&
              status.testset == "TESTS",
          "exact submission lookup continues beyond the first API page");
}

void test_process_and_normalization(const fs::path& root, const fs::path& executable) {
    check(cfx::split_command_words(R"(one "two three" 'four five')") ==
              std::vector<std::string>({"one", "two three", "four five"}),
          "command word splitting");
    check(cfx::normalize_output("one  \n\n two\t\n") == "one\n two", "output normalization");

    const cfx::ProcessResult timeout =
        cfx::run_process({"/bin/sh", "-c", "sleep 1"}, cfx::ProcessOptions{
                                                               .stdout_path = root / "timeout.out",
                                                               .stderr_path = root / "timeout.err",
                                                               .timeout =
                                                                   std::chrono::milliseconds(20),
                                                           });
    check(timeout.timed_out && timeout.status == 124, "process timeout");
    check(timeout.signal == SIGKILL && timeout.exit_code == -1,
          "timeout preserves the terminating signal");
    check(timeout.elapsed >= std::chrono::milliseconds(20), "timeout does not fire early");

    const cfx::ProcessResult failed =
        cfx::run_process({executable.string(), "--cfx-test-exit"});
    check(failed.status == 7 && failed.exit_code == 7 && failed.signal == 0,
          "normal nonzero exit is preserved");

    const cfx::ProcessResult signaled =
        cfx::run_process({executable.string(), "--cfx-test-signal"});
    check(signaled.status == 128 + SIGSEGV && signaled.exit_code == -1 &&
              signaled.signal == SIGSEGV,
          "fatal signal is preserved");

    const cfx::ProcessResult memory = cfx::run_process(
        {executable.string(), "--cfx-test-memory"},
        cfx::ProcessOptions{std::nullopt, root / "memory.out", root / "memory.err",
                            std::chrono::seconds(2), std::nullopt, 16U * 1024U * 1024U});
    check(memory.memory_limit_exceeded && memory.status == 125 && !memory.timed_out,
          "memory limit is classified separately from the kill signal");
    check(memory.peak_memory_bytes > 16U * 1024U * 1024U, "peak memory is measured");

    const cfx::ProcessResult output = cfx::run_process(
        {executable.string(), "--cfx-test-output"},
        cfx::ProcessOptions{std::nullopt, root / "output.out", root / "output.err",
                            std::chrono::seconds(2), std::nullopt, std::nullopt, 32U * 1024U});
    check(output.output_limit_exceeded && output.signal == SIGXFSZ,
          "output limit is classified separately from runtime error");
    check(fs::file_size(root / "output.out") <= 32U * 1024U, "output file is capped");

    const fs::path descendant_marker = root / "descendant";
    const cfx::ProcessResult descendant = cfx::run_process(
        {executable.string(), "--cfx-test-descendant", descendant_marker.string()});
    check(descendant.status == 0, "descendant fixture leader exits normally");
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    check(!fs::exists(descendant_marker.string() + ".survived"),
          "normal process exit cleans up descendants");

    const fs::path forwarded_marker = root / "forwarded-signal";
    const pid_t supervisor = ::fork();
    check(supervisor >= 0, "signal-forwarding fixture forks");
    if (supervisor == 0) {
        ::execl(executable.c_str(), executable.c_str(), "--cfx-test-forward-signal",
                forwarded_marker.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    for (int attempt = 0; attempt < 200 && !fs::is_regular_file(forwarded_marker); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(fs::is_regular_file(forwarded_marker), "signal-forwarding child starts");
    const pid_t worker = static_cast<pid_t>(std::stol(read(forwarded_marker)));
    check(::kill(supervisor, SIGTERM) == 0, "signal-forwarding supervisor is interrupted");
    int supervisor_status = 0;
    check(::waitpid(supervisor, &supervisor_status, 0) == supervisor,
          "signal-forwarding supervisor is reaped");
    check(WIFSIGNALED(supervisor_status) && WTERMSIG(supervisor_status) == SIGTERM,
          "interrupt is re-raised after forwarding");
    bool worker_gone = false;
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (::kill(worker, 0) != 0 && errno == ESRCH) {
            worker_gone = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(worker_gone, "interrupt cleans up the child process group");

    const auto detached_started = std::chrono::steady_clock::now();
    cfx::launch_detached_process({"/bin/sh", "-c", "sleep 1"});
    const auto detached_elapsed = std::chrono::steady_clock::now() - detached_started;
    check(detached_elapsed < std::chrono::milliseconds(500),
          "detached process returns without waiting for completion");
    check_throws(
        [] { cfx::launch_detached_process({"/definitely/missing/cfx-command"}); },
        "detached process reports exec failure");
}

void test_build_cache(const fs::path& root) {
    const cfx::Problem problem("90", "A", root);
    write(root / "include" / "value.hpp", "#pragma once\n#define VALUE 7\n");
    write(root / "include" / "unused.hpp", "#pragma once\n#define UNUSED 1\n");
    write(problem.solution_path(), "#include \"value.hpp\"\n"
                                   "#if PEEK_COMPILED != 1\n"
                                   "#error local build must enable peek\n"
                                   "#endif\n"
                                   "int main() { return VALUE == 7 ? 0 : 1; }\n");
    const cfx::Builder builder(root);
    const cfx::BuildResult first = builder.build_problem(problem);
    check(fs::is_regular_file(first.binary), "first cache build compiles");
    const auto modified = fs::last_write_time(first.binary);
    const cfx::BuildResult second = builder.build_problem(problem);
    check(first.binary == second.binary && fs::last_write_time(second.binary) == modified,
          "unchanged build uses cache");

    write(root / "include" / "unused.hpp", "#pragma once\n#define UNUSED 2\n");
    const cfx::BuildResult unused = builder.build_problem(problem);
    check(unused.binary == first.binary, "unused-header edit preserves the exact cache key");

    write(root / "include" / "value.hpp", "#pragma once\n#define VALUE 8\n");
    const cfx::BuildResult third = builder.build_problem(problem);
    check(third.binary != first.binary, "included-header edit changes the cache key");

    write(problem.solution_path(), "#ifndef LOCAL\n"
                                   "#error checked local build must define LOCAL\n"
                                   "#endif\n"
                                   "#if PEEK_COMPILED != 1\n"
                                   "#error checked local build must enable peek\n"
                                   "#endif\n"
                                   "int main() {}\n");
    const cfx::BuildResult checked_local =
        builder.build_problem(problem, cfx::BuildOptions{true, true});
    check(fs::is_regular_file(checked_local.binary),
          "checked local mode compiles with peek enabled");

    write(problem.solution_path(),
          "#ifdef LOCAL\n#error submission build must not define LOCAL\n#endif\n"
          "#if PEEK_COMPILED != 0\n#error submission build must disable peek\n#endif\n"
          "int main() {}\n");
    const cfx::BuildResult submission =
        builder.build_problem(problem, cfx::BuildOptions{true, false});
    check(fs::is_regular_file(submission.binary),
          "submission mode compiles without LOCAL and with peek disabled");

    const char* old_cfx_standard = std::getenv("CFX_STD");
    const char* old_standard = std::getenv("STD");
    const std::optional<std::string> saved_cfx_standard =
        old_cfx_standard == nullptr ? std::nullopt
                                    : std::optional<std::string>(old_cfx_standard);
    const std::optional<std::string> saved_standard =
        old_standard == nullptr ? std::nullopt : std::optional<std::string>(old_standard);
    ::unsetenv("CFX_STD");
    ::setenv("STD", "c++17", 1);
    check(cfx::configured_standard() == "c++20", "legacy STD does not configure compilation");
    ::setenv("CFX_STD", "c++23", 1);
    check(cfx::configured_standard() == "c++23", "CFX_STD configures compilation");
    if (saved_cfx_standard) {
        (void)::setenv("CFX_STD", saved_cfx_standard->c_str(), 1);
    } else {
        (void)::unsetenv("CFX_STD");
    }
    if (saved_standard) {
        (void)::setenv("STD", saved_standard->c_str(), 1);
    } else {
        (void)::unsetenv("STD");
    }
}

void test_companion_import(const fs::path& root) {
    write(root / "solution.cpp", "int main() {}\n");
    const std::string payload = R"({"name":"Way Too Long Words",)"
                                R"("url":"https://codeforces.com/contest/71/problem/A",)"
                                R"("timeLimit":1000,"memoryLimit":256,)"
                                R"("tests":[{"input":"1\nword\n","output":"word\n"}]})";
    const cfx::CompanionPackage package = cfx::parse_companion_package(payload, root);
    check(package.problem.id() == "71A", "Companion URL parsing");
    cfx::import_companion_package(package, root);
    check(read(package.problem.samples_path() / "01.in") == "1\nword\n", "Companion input content");
    cfx::import_companion_package(package, root);

    write(package.problem.samples_path() / "02.in", "stale\n");
    write(package.problem.samples_path() / "02.out", "stale\n");
    cfx::import_companion_package(package, root);
    check(!fs::exists(package.problem.samples_path() / "02.in") &&
              !fs::exists(package.problem.samples_path() / "02.out"),
          "refresh removes stale pairs");

    cfx::CompanionPackage changed = package;
    changed.samples.front().input = "different\n";
    cfx::import_companion_package(changed, root);
    check(read(package.problem.samples_path() / "01.in") == "different\n", "refreshed input");
}

void test_problem_limits_and_verdicts(const fs::path& root) {
    const cfx::Problem problem("89", "A", root);
    write(problem.solution_path(),
          "#include <iostream>\n"
          "#include <string>\n"
          "#include <unistd.h>\n"
          "int main() { std::string mode; std::cin >> mode; "
          "if (mode == \"AC\") { std::cout << \"yes\\n\"; return 0; } "
          "if (mode == \"WA\") { std::cout << \"no\\n\"; return 0; } "
          "if (mode == \"RE\") return 7; for (;;) ::pause(); }\n");
    write(problem.directory() / "problem.json",
          "{\"id\":\"89A\",\"timeLimitMs\":1000,\"memoryLimitMb\":256}\n");
    for (const auto& [number, input] :
         std::vector<std::pair<std::string, std::string>>{{"01", "AC\n"}, {"02", "WA\n"},
                                                          {"03", "RE\n"}, {"04", "TLE\n"}}) {
        write(problem.samples_path() / (number + ".in"), input);
        write(problem.samples_path() / (number + ".out"), "yes\n");
    }

    const cfx::ProblemLimits limits = cfx::load_problem_limits(problem);
    check(limits.time_limit == std::chrono::seconds(1) && limits.time_from_metadata,
          "time limit is loaded from problem metadata");
    check(limits.memory_limit_bytes == 256U * 1024U * 1024U && limits.memory_from_metadata,
          "memory limit is loaded from problem metadata");

    const cfx::TestSummary summary = cfx::Judge(root).test(problem);
    check(summary.total == 4 && summary.passed == 1 && !summary.success(),
          "judge distinguishes accepted and failed cases");

    const cfx::Problem input_only("89", "C", root);
    write(input_only.solution_path(), "int main() {}\n");
    write(input_only.samples_path() / "01.in", "\n");
    const cfx::TestSummary input_only_summary = cfx::Judge(root).test(input_only);
    check(!input_only_summary.success() && input_only_summary.total == 1,
          "testing requires expected output");

    for (const std::string number : {"02", "03", "04"}) {
        fs::remove(problem.samples_path() / (number + ".in"));
        fs::remove(problem.samples_path() / (number + ".out"));
    }
    cfx::TestOptions overridden;
    overridden.timeout = std::chrono::seconds(2);
    overridden.memory_limit_bytes = 512U * 1024U * 1024U;
    const cfx::TestSummary override_summary = cfx::Judge(root).test(problem, overridden);
    check(override_summary.success(), "explicit test limits remain usable");

    const cfx::Problem without_metadata("89", "B", root);
    check(cfx::load_problem_limits(without_metadata).time_limit == std::chrono::seconds(5),
          "missing metadata uses the default time limit");
    write(problem.directory() / "problem.json",
          "{\"id\":\"89A\",\"timeLimitMs\":0,\"memoryLimitMb\":256}\n");
    check_throws([&] { (void)cfx::load_problem_limits(problem); },
                 "invalid problem limits are rejected");
}

void test_submission_preparation(const fs::path& root) {
    const cfx::Problem problem("88", "A", root);
    write(problem.solution_path(),
          "#include <iostream>\n"
          "#ifdef LOCAL\n"
          "constexpr bool local_build = true;\n"
          "#else\n"
          "constexpr bool local_build = false;\n"
          "#endif\n"
          "#if PEEK_COMPILED != 0\n"
          "#error submission preparation must disable peek\n"
          "#endif\n"
          "int main() { int value = 0; std::cin >> value; "
          "std::cout << value + (local_build ? 0 : 0) << '\\n'; }\n");
    write(problem.samples_path() / "01.in", "7\n");
    write(problem.samples_path() / "01.out", "7\n");

    const cfx::SubmissionArtifact artifact = cfx::prepare_submission(root, problem);
    check(fs::is_regular_file(artifact.source), "submission artifact exists");
    check(artifact.source_text == read(artifact.source), "submission carries the pinned source");
    check(read(artifact.source.parent_path() / "solution.cpp") == read(problem.solution_path()),
          "authored snapshot matches the solution");
    check(artifact.target == "88A", "submission target");
    check(artifact.language == "GNU C++20", "submission language");
    check(artifact.page_url == "https://codeforces.com/contest/88/submit",
          "submission uses the contest page");
    check(artifact.source_hash.size() == 32, "submission hash");
    check(artifact.source_hash == cfx::content_digest(artifact.source_text),
          "submission hash matches pinned source");
    check(artifact.source.parent_path().filename().string().find(
              artifact.source_hash.substr(0, 16)) != std::string::npos,
          "submission artifact directory contains hash");

    write(problem.samples_path() / "01.out", "8\n");
    check_throws([&] { (void)cfx::prepare_submission(root, problem); },
                 "ordinary submission rejects a textual mismatch");
    write(problem.samples_path() / "01.out", "7\n");

    const cfx::Problem untested("88", "B", root);
    write(untested.solution_path(), "int main() {}\n");
    check_throws([&] { (void)cfx::prepare_submission(root, untested); },
                 "submission requires at least one complete test pair");
    write(untested.samples_path() / "01.in", "\n");
    check_throws([&] { (void)cfx::prepare_submission(root, untested); },
                 "submission rejects an input-only test");

    const cfx::Problem slow("88", "C", root);
    write(slow.solution_path(), "#include <unistd.h>\nint main() { for (;;) ::pause(); }\n");
    write(slow.samples_path() / "01.in", "\n");
    write(slow.samples_path() / "01.out", "\n");
    write(slow.directory() / "problem.json",
          "{\"id\":\"88C\",\"timeLimitMs\":20,\"memoryLimitMb\":256}\n");
    check_throws([&] { (void)cfx::prepare_submission(root, slow); },
                 "submission rejects a metadata-driven time-limit failure");
}

void test_browser_page_validation() {
    cfx::BrowserBridgeOptions options;
    options.extension_id = "abcdefghijklmnopabcdefghijklmnop";
    check_throws(
        [&] {
            (void)cfx::fetch_problem_in_browser("https://example.com/contest/71/problem/A",
                                                options);
        },
        "browser bridge must reject a foreign fetch page");
    check_throws(
        [&] {
            (void)cfx::fetch_problem_in_browser(
                "https://codeforces.com.evil.example/contest/71/problem/A", options);
        },
        "browser bridge must reject an origin-prefix lookalike");
    check_throws(
        [&] {
            (void)cfx::submit_in_browser(cfx::BrowserSubmitRequest{
                "https://codeforces.com/contest/72/submit",
                "71A",
                "A",
                "GNU C++20",
                "int main() {}\n",
            }, options);
        },
        "browser bridge must reject a mismatched contest submission page");
}

void test_browser_submission_states() {
    if (std::getenv("CFX_TEST_BROWSER_LOG") == nullptr ||
        std::getenv("CFX_TEST_SUBMISSION_PAYLOAD") == nullptr) {
        return;
    }

    cfx::BrowserBridgeOptions options;
    check(cfx::BrowserBridgeOptions{}.connect_timeout == std::chrono::seconds(30),
          "default browser grace period covers a cold Chrome launch");
    options.request_timeout = std::chrono::seconds(1);
    options.connect_timeout = std::chrono::seconds(3);
    options.wait_timeout = std::chrono::seconds(5);
    options.verdict_poll_interval = std::chrono::milliseconds(10);
    options.extension_id = "abcdefghijklmnopabcdefghijklmnop";
    const cfx::BrowserSubmitRequest request{
        "https://codeforces.com/contest/99993/submit",
        "99993C",
        "C",
        "GNU C++20",
        "int main() {}\n",
    };

    ::setenv("CFX_TEST_CONNECTOR_MODE", "ready-only", 1);
    bool unavailable = false;
    try {
        (void)cfx::submit_in_browser(request, options);
    } catch (const cfx::BrowserConnectorUnavailable&) {
        unavailable = true;
    }
    check(unavailable, "ready without a source request is safe to fall back");

    ::setenv("CFX_TEST_CONNECTOR_MODE", "pre-submit-error", 1);
    bool sign_in_required = false;
    try {
        (void)cfx::submit_in_browser(request, options);
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        sign_in_required = message.find("Codeforces submission failed") != std::string::npos &&
                           message.find("not signed in") != std::string::npos;
    }
    check(sign_in_required, "known sign-in failure is reported before source is requested");

    ::setenv("CFX_TEST_CONNECTOR_MODE", "source-only", 1);
    bool source_unknown = false;
    try {
        (void)cfx::submit_in_browser(request, options);
    } catch (const cfx::BrowserConnectorUnavailable&) {
        check(false, "served source must never trigger automatic fallback");
    } catch (const std::runtime_error& error) {
        source_unknown = std::string(error.what()).find("status is unknown") != std::string::npos;
    }
    check(source_unknown, "missing result after serving source has unknown status");

    ::setenv("CFX_TEST_CONNECTOR_MODE", "unknown-result", 1);
    bool reported_unknown = false;
    try {
        (void)cfx::submit_in_browser(request, options);
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        reported_unknown = message.find("submission status is unknown") != std::string::npos &&
                           message.find("submission 123456789") != std::string::npos &&
                           message.find("/submission/123456789") != std::string::npos &&
                           message.find("Codeforces submission failed") == std::string::npos;
    }
    check(reported_unknown, "uncertain browser result is not reported as a safe rejection");

    SubmissionApiServer pending_api({
        R"({"status":"OK","result":[]})",
    });
    cfx::BrowserBridgeOptions pending_options = options;
    pending_options.wait_timeout = std::chrono::seconds(2);
    ::setenv("CFX_API_BASE", pending_api.base_url().c_str(), 1);
    ::setenv("CFX_TEST_CONNECTOR_MODE", "success", 1);
    const cfx::BrowserSubmitReceipt pending =
        cfx::submit_in_browser(request, pending_options);
    pending_api.wait();
    check(pending.submission_id == "123456789" && pending.handle == "panicsort" &&
              pending.verdict.empty() && !pending.pending_reason.empty(),
          "confirmed submission identity survives a verdict polling timeout");

    SubmissionApiServer api({
        R"({"status":"OK","result":[]})",
        R"({"status":"OK","result":[{"id":123456789,"contestId":99993,)"
        R"("problem":{"contestId":99993,"index":"C"},)"
        R"("author":{"members":[{"handle":"panicsort"}],"participantType":"CONTESTANT"},)"
        R"("verdict":"TESTING"}]})",
        R"({"status":"OK","result":[{"id":123456789,"contestId":99993,)"
        R"("problem":{"contestId":99993,"index":"C"},)"
        R"("author":{"members":[{"handle":"panicsort"}],"participantType":"CONTESTANT"},)"
        R"("verdict":"TIME_LIMIT_EXCEEDED","testset":"TESTS","passedTestCount":2,)"
        R"("timeConsumedMillis":1000,"memoryConsumedBytes":204800}]})",
    });
    ::setenv("CFX_API_BASE", api.base_url().c_str(), 1);
    ::setenv("CFX_TEST_CONNECTOR_MODE", "tle-result", 1);
    const cfx::BrowserSubmitReceipt tle = cfx::submit_in_browser(request, options);
    api.wait();
    check(tle.submission_id == "123456789" && tle.verdict == "TIME_LIMIT_EXCEEDED" &&
              tle.handle == "panicsort" && tle.participant_type == "CONTESTANT" &&
              tle.testset == "TESTS" && tle.pending_reason.empty() &&
              tle.verdict_text == "Time Limit Exceeded" && tle.passed_test_count == 2 &&
              tle.time_consumed_millis == 1000 && tle.memory_consumed_bytes == 204800 &&
              tle.judging_wait_millis >= 2000 && tle.judging_wait_millis < 8000,
          "terminal judge failure is returned with complete judge details");
    ::unsetenv("CFX_API_BASE");
    ::unsetenv("CFX_TEST_CONNECTOR_MODE");
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--cfx-test-forward-signal") {
        const cfx::ProcessResult result = cfx::run_process(
            {argv[0], "--cfx-test-forward-worker", argv[2]});
        return result.status;
    }
    if (argc == 3 && std::string_view(argv[1]) == "--cfx-test-forward-worker") {
        write(argv[2], std::to_string(::getpid()) + "\n");
        for (;;) {
            ::pause();
        }
    }
    if (argc == 3 && std::string_view(argv[1]) == "--cfx-test-descendant") {
        const pid_t descendant = ::fork();
        if (descendant < 0) {
            return 1;
        }
        if (descendant == 0) {
            ::usleep(200000);
            write(std::string(argv[2]) + ".survived", "survived\n");
            _exit(0);
        }
        write(argv[2], std::to_string(descendant) + "\n");
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--cfx-test-exit") {
        return 7;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--cfx-test-signal") {
        std::raise(SIGSEGV);
        return 1;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--cfx-test-memory") {
        std::vector<char> memory(64U * 1024U * 1024U, 1);
        while (memory.front() != 0) {
            ::pause();
        }
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--cfx-test-output") {
        const std::string block(4096, 'x');
        while (::write(STDOUT_FILENO, block.data(), block.size()) > 0) {
        }
        return 0;
    }
    try {
        TemporaryDirectory temporary;
        test_json_and_codeforces();
        test_codeforces_poll_interval();
        test_codeforces_status_pagination();
        test_process_and_normalization(temporary.path(), argv[0]);
        test_build_cache(temporary.path() / "build");
        test_companion_import(temporary.path() / "companion");
        test_problem_limits_and_verdicts(temporary.path() / "verdicts");
        test_submission_preparation(temporary.path() / "submission");
        test_browser_page_validation();
        test_browser_submission_states();
        std::cout << "tool integration tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "tool test failed: " << error.what() << '\n';
        return 1;
    }
}
