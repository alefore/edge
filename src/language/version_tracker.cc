#include "src/language/version_tracker.h"

namespace afc::language::staging {
Revision Tracker::max_clean() const { return max_clean_; }

Revision Tracker::active() const { return active_; }

void Tracker::MarkClean(Revision revision) {
  CHECK_LT(revision, active_);
  max_clean_ = std::max(max_clean_, revision);
}

void Tracker::StartStagingRevision() { ++active_; }
}  // namespace afc::language::staging
