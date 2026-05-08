// A VersionTracker keeps track of the "cleanliness" state of some data we load
// from storage and modify locally. It helps us remember which of the local
// modifications have been saved successfully.
#ifndef __AFC_EDITOR_SRC_LANGUAGE_VERSION_VALUE_H__
#define __AFC_EDITOR_SRC_LANGUAGE_VERSION_VALUE_H__

#include "src/language/ghost_type_class.h"

namespace afc::language::staging {
// Monotonically increasing version of a "staging" area where changes are
// grouped (and appied together).
struct Revision : public language::GhostType<Revision, size_t> {
  using GhostType::GhostType;
};

// Version for data that doesn't come from a staging source but rather from a
// canonical source.
struct Clean_t {
  explicit Clean_t() = default;
};
inline constexpr Clean_t Clean{};

using Origin = std::variant<Clean_t, Revision>;

template <typename T>
struct Value {
  Origin origin;
  T value;
};
}  // namespace afc::language::staging
#endif  // __AFC_EDITOR_SRC_LANGUAGE_VERSION_VALUE_H__
