#include "src/language/version_tracker.h"

namespace afc::language::version {
VersionId Tracker::last_clean() const { return last_clean_; }

VersionId Tracker::staging() const { return staging_; }

void Tracker::MarkClean(VersionId version) {
  CHECK_LT(version, staging_);
  last_clean_ = std::max(last_clean_, version);
}

VersionId Tracker::NewStagingVersion() { return staging_++; }
}  // namespace afc::language::version
