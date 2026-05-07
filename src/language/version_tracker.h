// A VersionTracker keeps track of the "cleanliness" state of some data we load
// from storage and modify locally. It helps us remember which of the local
// modifications have been saved successfully.
#ifndef __AFC_EDITOR_SRC_LANGUAGE_VERSION_TRACKER_H__
#define __AFC_EDITOR_SRC_LANGUAGE_VERSION_TRACKER_H__

#include "src/language/ghost_type_class.h"
#include "src/language/version_value.h"

namespace afc::language::version {
enum class SyncState {
  // The value originated at the canonical data source.
  Clean,
  // The value originates locally (should be considered dirty).
  Dirty
};

class Tracker {
  // The largest local version id known to be clean.
  VersionId last_clean_ = VersionId{0};

  // The current version we're producing. DAta with ValueSource::Local should be
  // marked with this version.
  //
  // Must be greater than master_version_.
  VersionId staging_ = VersionId{1};

 public:
  VersionId last_clean() const;

  VersionId staging() const;

  // Marks a given version as clean. This should be done when the corresponding
  // snapshot is stored.
  void MarkClean(VersionId);

  // Signals that a save operation has started and future modifications should
  // remain dirty even after that save operation concludes.
  //
  // Returns the previous value of `local_version`, which should be passed to
  // `MarkClean` when/if the operation succeeds.
  VersionId NewStagingVersion();
};
}  // namespace afc::language::version
#endif  // __AFC_EDITOR_SRC_LANGUAGE_VERSION_TRACKER_H__
