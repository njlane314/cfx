#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace cfx {

class ProblemError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class Problem {
  public:
    Problem(std::string contest_id, std::string index,
            std::filesystem::path root = std::filesystem::current_path());

    static Problem parse(std::string_view value,
                         const std::filesystem::path& root = std::filesystem::current_path());
    static std::optional<Problem>
    infer(const std::filesystem::path& location,
          const std::filesystem::path& root = std::filesystem::current_path());

    [[nodiscard]] const std::string& contest_id() const noexcept;
    [[nodiscard]] const std::string& index() const noexcept;
    [[nodiscard]] std::string id() const;
    [[nodiscard]] std::filesystem::path directory() const;
    [[nodiscard]] std::filesystem::path solution_path() const;
    [[nodiscard]] std::filesystem::path metadata_path() const;
    [[nodiscard]] std::filesystem::path state_directory() const;
    [[nodiscard]] std::filesystem::path samples_path() const;
    [[nodiscard]] std::filesystem::path cases_path() const;
    [[nodiscard]] std::vector<std::filesystem::path> test_directories() const;

  private:
    std::string contest_id_;
    std::string index_;
    std::filesystem::path root_;
};

[[nodiscard]] std::filesystem::path
find_workspace_root(const std::filesystem::path& start = std::filesystem::current_path());

class WorkspaceError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class Workspace {
  public:
    explicit Workspace(std::filesystem::path root = std::filesystem::current_path());
    std::filesystem::path create(const Problem& problem) const;

  private:
    std::filesystem::path root_;
};

void remember_current_problem(const Problem& problem,
                              const std::filesystem::path& root = std::filesystem::current_path());
[[nodiscard]] std::optional<Problem>
current_problem(const std::filesystem::path& root = std::filesystem::current_path());

class JsonError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class Json {
  public:
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json, std::less<>>;
    using Value = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    Json();
    explicit Json(Value value);
    [[nodiscard]] bool is_null() const;
    [[nodiscard]] bool is_bool() const;
    [[nodiscard]] bool is_number() const;
    [[nodiscard]] bool is_string() const;
    [[nodiscard]] bool is_array() const;
    [[nodiscard]] bool is_object() const;
    [[nodiscard]] bool boolean() const;
    [[nodiscard]] double number() const;
    [[nodiscard]] const std::string& string() const;
    [[nodiscard]] const Array& array() const;
    [[nodiscard]] const Object& object() const;
    [[nodiscard]] const Json* find(std::string_view key) const;
    [[nodiscard]] const Json& at(std::string_view key) const;

  private:
    Value value_;
};

Json parse_json(std::string_view input);
std::string json_quote(std::string_view value);

[[nodiscard]] std::string read_text(const std::filesystem::path& path);
void write_text(const std::filesystem::path& path, std::string_view contents);
void write_atomic(const std::filesystem::path& path, std::string_view contents);
[[nodiscard]] std::filesystem::path state_root(const std::filesystem::path& archive_root);
std::string content_digest(std::string_view value);

class BundleError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::string
bundle(const std::filesystem::path& source,
       const std::filesystem::path& root = std::filesystem::current_path());

struct Sample {
    std::string input;
    std::string output;
};

struct CompanionPackage {
    Problem problem;
    std::string url;
    std::string name;
    int time_limit_ms = 0;
    int memory_limit_mb = 0;
    std::vector<Sample> samples;
};

CompanionPackage parse_companion_package(std::string_view payload,
                                         const std::filesystem::path& root);
void import_companion_package(const CompanionPackage& package,
                              const std::filesystem::path& root);

struct ProcessResult {
    int status = 0;
    int exit_code = 0;
    int signal = 0;
    bool timed_out = false;
    bool memory_limit_exceeded = false;
    bool output_limit_exceeded = false;
    std::chrono::milliseconds elapsed{0};
    std::chrono::milliseconds cpu_time{0};
    std::uint64_t peak_memory_bytes = 0;
};

struct ProcessOptions {
    std::optional<std::filesystem::path> stdin_path = std::nullopt;
    std::optional<std::filesystem::path> stdout_path = std::nullopt;
    std::optional<std::filesystem::path> stderr_path = std::nullopt;
    std::optional<std::chrono::milliseconds> timeout = std::nullopt;
    std::optional<std::filesystem::path> working_directory = std::nullopt;
    std::optional<std::uint64_t> memory_limit_bytes = std::nullopt;
    std::optional<std::uint64_t> output_limit_bytes = std::nullopt;
};

ProcessResult run_process(const std::vector<std::string>& arguments,
                          const ProcessOptions& options = {});
void launch_detached_process(const std::vector<std::string>& arguments);

struct CaptureResult {
    int status = 0;
    std::string output;
};

CaptureResult capture_process(const std::vector<std::string>& arguments);
std::vector<std::string> split_command_words(const std::string& value);

struct BuildOptions {
    bool checked = false;
    bool local = true;
};

struct BuildResult {
    std::filesystem::path binary;
    std::filesystem::path bundled_source;
};

class Builder {
  public:
    explicit Builder(std::filesystem::path root);
    std::string bundled_source(const std::filesystem::path& source) const;
    BuildResult build_problem(const Problem& problem, const BuildOptions& options = {}) const;

  private:
    std::filesystem::path root_;
};

std::string format_bytes(std::uintmax_t bytes);
std::string configured_standard();

struct ProblemLimits {
    std::chrono::milliseconds time_limit{5000};
    std::optional<std::uint64_t> memory_limit_bytes;
    bool time_from_metadata = false;
    bool memory_from_metadata = false;
};

struct TestOptions {
    bool checked = false;
    std::optional<std::chrono::milliseconds> timeout;
    bool concise = false;
    std::optional<std::uint64_t> memory_limit_bytes;
    std::optional<std::uint64_t> output_limit_bytes{64U * 1024U * 1024U};
    bool submission_profile = false;
};

struct TestSummary {
    BuildResult build;
    int passed = 0;
    int total = 0;
    [[nodiscard]] bool success() const noexcept { return passed == total; }
};

class Judge {
  public:
    explicit Judge(std::filesystem::path root);
    TestSummary test(const Problem& problem, const TestOptions& options = {}) const;

  private:
    std::filesystem::path root_;
    Builder builder_;
};

std::string normalize_output(const std::string& output);
std::string format_duration(std::chrono::milliseconds duration);
ProblemLimits load_problem_limits(const Problem& problem);

struct CodeforcesSubmission {
    bool found = false;
    bool terminal = false;
    std::string verdict;
    std::string verdict_text;
    std::string handle;
    std::string participant_type;
    std::string testset;
    std::uint64_t passed_test_count = 0;
    std::uint64_t time_consumed_millis = 0;
    std::uint64_t memory_consumed_bytes = 0;
};

struct ProblemSuggestion {
    Problem problem;
    std::string name;
    int rating;
    std::vector<std::string> tags;
};

CodeforcesSubmission poll_submission_status(
    const std::string& contest_id, const std::string& problem_index,
    const std::string& handle, const std::string& submission_id,
    const std::chrono::steady_clock::time_point& deadline,
    std::chrono::milliseconds interval = std::chrono::milliseconds(2100));
std::pair<std::vector<ProblemSuggestion>, int>
pick_problems(const std::filesystem::path& root, int rating, std::size_t count,
              const std::string& handle, const std::vector<std::string>& tags);
std::string problem_url(const Problem& problem);
std::string submission_url(const Problem& problem);

struct SubmissionArtifact {
    std::filesystem::path source;
    std::string source_text;
    std::string source_hash;
    std::string target;
    std::string language;
    std::string page_url;
};

SubmissionArtifact prepare_submission(const std::filesystem::path& root, const Problem& problem);
void copy_submission_to_clipboard(const SubmissionArtifact& artifact);

class BrowserConnectorUnavailable : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

struct BrowserSubmitRequest {
    std::string page_url;
    std::string target;
    std::string index;
    std::string language;
    std::string source;
};

struct BrowserSubmitReceipt {
    std::string submission_url;
    std::string submission_id;
    std::string verdict;
    std::string verdict_text;
    std::string handle;
    std::string participant_type;
    std::string testset;
    std::string pending_reason;
    std::uint64_t passed_test_count = 0;
    std::uint64_t time_consumed_millis = 0;
    std::uint64_t memory_consumed_bytes = 0;
    std::uint64_t judging_wait_millis = 0;
};

struct BrowserBridgeOptions {
    std::chrono::milliseconds request_timeout{5000};
    std::chrono::milliseconds connect_timeout{30000};
    std::chrono::milliseconds wait_timeout{370000};
    std::chrono::milliseconds verdict_poll_interval{2100};
    std::size_t max_source_bytes = 1024 * 1024;
    std::size_t max_fetch_bytes = 16 * 1024 * 1024;
    std::size_t max_result_bytes = 64 * 1024;
    std::size_t max_connections = 32;
    std::string extension_id;
};

void open_browser_url(const std::string& url);
std::string fetch_problem_in_browser(const std::string& page_url,
                                     const BrowserBridgeOptions& options = {});
BrowserSubmitReceipt submit_in_browser(const BrowserSubmitRequest& request,
                                       const BrowserBridgeOptions& options = {});

namespace browser_http {

using Headers = std::map<std::string, std::string, std::less<>>;

class Error final : public std::runtime_error {
  public:
    Error(int status, std::string message);
    [[nodiscard]] int status() const noexcept;

  private:
    int status_;
};

struct RequestHead {
    std::string method;
    std::string target;
    Headers headers;
    std::optional<std::size_t> content_length;
};

struct Request : RequestHead { std::string body; };
struct Response {
    int status = 200;
    Headers headers;
    std::string body;
};

[[nodiscard]] std::string header(const RequestHead& request, std::string_view name);

class Connection {
  public:
    Connection(Connection&& other) noexcept;
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Request read(const std::chrono::steady_clock::time_point& deadline,
                 const std::function<std::size_t(const RequestHead&)>& body_limit);
    void send(const Response& response) const;

  private:
    friend class Server;
    explicit Connection(int descriptor);
    int descriptor_ = -1;
};

class Server {
  public:
    Server();
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    [[nodiscard]] std::uint16_t port() const noexcept;
    std::optional<Connection>
    accept_until(const std::chrono::steady_clock::time_point& deadline,
                 std::chrono::milliseconds request_timeout) const;

  private:
    int descriptor_ = -1;
    std::uint16_t port_ = 0;
};

} // namespace browser_http
} // namespace cfx
