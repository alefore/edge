// A VersionTracker keeps track of the "cleanliness" state of some data we load
// from storage and modify locally. It helps us remember which of the local
// modifications have been saved successfully.
#ifndef __AFC_EDITOR_SRC_LANGUAGE_VERSION_VALUE_H__
#define __AFC_EDITOR_SRC_LANGUAGE_VERSION_VALUE_H__

#include "src/language/ghost_type_class.h"

namespace afc::language::version {
struct VersionId : public language::GhostType<VersionId, size_t> {
  using GhostType::GhostType;
};

template <typename Value>
struct VersionValue {
  VersionId version_id;
  Value value;
};
}  // namespace afc::language::version
#endif  // __AFC_EDITOR_SRC_LANGUAGE_VERSION_VALUE_H__
