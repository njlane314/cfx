#pragma once

#include <filesystem>
#include <string>

namespace cfx {

class Problem;

struct SubmissionArtifact {
    // Immutable snapshots captured before the browser submission begins.
    std::filesystem::path source;
    std::string source_text;
    std::string source_hash;
    std::filesystem::path authored_source;
    std::string authored_source_text;
    std::string authored_source_hash;
    std::string target;
    std::string language;
    std::string page_url;
};

SubmissionArtifact prepare_submission(const std::filesystem::path& root, const Problem& problem,
                                      bool rebuild = false);

void copy_submission_to_clipboard(const SubmissionArtifact& artifact);

} // namespace cfx
