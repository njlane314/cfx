#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cfx {

class Problem;
struct BrowserSubmitReceipt;
struct CodeforcesSubmission;
struct SubmissionArtifact;

enum class SubmissionState {
    pending,
    pretests,
    accepted,
    rejected,
};

struct RecordedSubmission {
    std::filesystem::path receipt;
    std::optional<std::string> commit;
    SubmissionState state = SubmissionState::pending;
};

struct StoredSubmission {
    std::string problem_id;
    std::string submission_id;
    std::string handle;
    SubmissionState state = SubmissionState::pending;
    std::filesystem::path directory;
};

// Preserve the exact submitted source and verdict. A local commit is created
// only for a final Accepted verdict and only when recording is enabled.
RecordedSubmission record_submission(const std::filesystem::path& archive_root,
                                     const Problem& problem,
                                     const SubmissionArtifact& artifact,
                                     const BrowserSubmitReceipt& receipt);

// Return submissions that still need a final verdict or an acceptance commit.
std::vector<StoredSubmission>
unresolved_submissions(const std::filesystem::path& archive_root);

// Reconcile one stored submission with a fresh Codeforces status. Never pushes.
RecordedSubmission update_submission(const std::filesystem::path& archive_root,
                                     const StoredSubmission& stored,
                                     const CodeforcesSubmission& status);

// Retry the local commit for an already accepted stored submission. Never polls or pushes.
RecordedSubmission retry_submission_recording(const std::filesystem::path& archive_root,
                                              const StoredSubmission& stored);

} // namespace cfx
