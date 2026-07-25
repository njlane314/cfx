#pragma once

#include <filesystem>
#include <string>

namespace cfprobs {

class Problem;

struct SubmissionArtifact {
    std::filesystem::path source;
    std::string source_text;
    std::string source_hash;
    std::string target;
    std::string language;
    std::string page_url;
};

SubmissionArtifact prepare_submission(const std::filesystem::path& root, const Problem& problem,
                                      bool rebuild = false);

void copy_submission_to_clipboard(const SubmissionArtifact& artifact);

} // namespace cfprobs
