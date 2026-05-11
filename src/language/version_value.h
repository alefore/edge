// A VersionTracker keeps track of the "cleanliness" state of some data we load
// from storage and modify locally. It helps us remember which of the local
// modifications have been saved successfully.
#ifndef __AFC_EDITOR_SRC_LANGUAGE_VERSION_VALUE_H__
#define __AFC_EDITOR_SRC_LANGUAGE_VERSION_VALUE_H__

#include <ranges>

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
bool operator==(const Clean_t&, const Clean_t&);

using Origin = std::variant<Clean_t, Revision>;

// Returns the largest of two origin specifications. If two lines with origins
// a and b are getting merged, returns the origin that the merged line should
// use.
Origin MergeOrigins(Origin a, Origin b);

template <typename T>
struct Value {
  Origin origin;
  T value;

  const T* operator->() const { return &value; }
  T* operator->() { return &value; }
};

template <typename T>
auto CleanValue(T&& value) -> Value<std::decay_t<T>> {
  return Value<std::decay_t<T>>{.origin = Clean,
                                .value = std::forward<T>(value)};
}

struct AddOrigin {
  Origin origin;

  constexpr explicit AddOrigin(Origin o) : origin(o) {}

  // The magic happens here: pipe operator support
  template <std::ranges::viewable_range R>
  constexpr auto operator()(R&& r) const {
    return std::forward<R>(r) |
           std::views::transform([origin = origin](auto&& value) {
             using T = std::decay_t<decltype(value)>;
             return Value<T>{.origin = origin,
                             .value = std::forward<decltype(value)>(value)};
           });
  }

  template <std::ranges::viewable_range R>
  friend constexpr auto operator|(R&& r, const AddOrigin& closure) {
    return closure(std::forward<R>(r));
  }
};
}  // namespace afc::language::staging
#endif  // __AFC_EDITOR_SRC_LANGUAGE_VERSION_VALUE_H__
