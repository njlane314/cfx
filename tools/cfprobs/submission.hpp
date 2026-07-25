#pragma once

#include <filesystem>
#include <string>

namespace cfprobs {

class Problem;

struct SubmissionArtifact {
    std::filesystem::path source;
    std::string source_hash;
    std::string target;
    std::string language;
    std::string page_url;
};

SubmissionArtifact prepare_submission(const std::filesystem::path& root, const Problem& problem,
                                      bool rebuild = false);

class SubmitTransport {
  public:
    virtual ~SubmitTransport() = default;
    virtual void handoff(const SubmissionArtifact& artifact, bool open_browser) const = 0;
};

class BrowserAssistedTransport final : public SubmitTransport {
  public:
    void handoff(const SubmissionArtifact& artifact, bool open_browser) const override;
};

} // namespace cfprobs
