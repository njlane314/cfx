#pragma once

#include <chrono>
#include <cstddef>
#include <string>

namespace cfprobs {

struct BrowserSubmitRequest {
    std::string page_url;
    std::string target;
    std::string index;
    std::string language;
    std::string source;
};

struct BrowserSubmitReceipt {
    std::string submission_url;
    std::string verdict;
};

struct BrowserBridgeOptions {
    std::chrono::milliseconds request_timeout{5000};
    std::chrono::milliseconds wait_timeout{120000};
    std::size_t max_source_bytes = 1024 * 1024;
    std::size_t max_fetch_bytes = 16 * 1024 * 1024;
    std::size_t max_result_bytes = 64 * 1024;
    std::size_t max_connections = 32;
};

// Opens a Codeforces problem page and waits for the browser extension to send
// its rendered metadata and sample tests as Competitive Companion JSON.
std::string fetch_problem_in_browser(const std::string& page_url,
                                     const BrowserBridgeOptions& options = {});

// Opens the Codeforces submission page and lets the browser extension submit
// through that page's existing authenticated session.
BrowserSubmitReceipt submit_in_browser(const BrowserSubmitRequest& request,
                                       const BrowserBridgeOptions& options = {});

} // namespace cfprobs
