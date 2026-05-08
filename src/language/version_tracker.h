// A VersionTracker keeps track of the "cleanliness" state of some data we load
// from storage and modify locally. It helps us remember which of the local
// modifications have been saved successfully.
#ifndef __AFC_EDITOR_SRC_LANGUAGE_VERSION_TRACKER_H__
#define __AFC_EDITOR_SRC_LANGUAGE_VERSION_TRACKER_H__

#include "src/language/ghost_type_class.h"
#include "src/language/version_value.h"

namespace afc::language::staging {
class Tracker {
  // The revision of the largest staging area known to be clean.
  Revision max_clean_ = Revision{0};

  // Holds the currently active staging revision, which should be used by all
  // new values.
  //
  // Must be greater than max_clean_.
  Revision active_ = Revision{1};

 public:
  Revision max_clean() const;

  Revision active() const;

  template <typename T>
  Value<T> NewStagingValue(T value) {
    return Value<T>{.origin = active(), .value = std::move<T>(value)};
  }

  // Marks a given revision as clean (e.g., successfully saved).
  void MarkClean(Revision);

  // Creates a new staging revision. All future modifications must occur inside
  // it (until `NewStagingRevision` is called again).
  void StartStagingRevision();
};
}  // namespace afc::language::staging
#endif  // __AFC_EDITOR_SRC_LANGUAGE_VERSION_TRACKER_H__
