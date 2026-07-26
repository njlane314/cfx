#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace cfx {

class Problem;
struct BrowserSubmitReceipt;
struct SubmissionArtifact;

struct AcceptanceRecord {
    std::filesystem::path receipt;
    std::optional<std::string> commit;
};

// Persist the confirmed verdict and, when `git config cfx.record commit` is
// enabled in the archive, create one local commit. This function never pushes.
AcceptanceRecord record_acceptance(const std::filesystem::path& archive_root,
                                   const Problem& problem,
                                   const SubmissionArtifact& artifact,
                                   const BrowserSubmitReceipt& receipt);

} // namespace cfx
