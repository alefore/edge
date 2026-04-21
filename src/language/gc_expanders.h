// Various templated `Expand` implementations expected to be generally useful.

#ifndef __AFC_LANGUAGE_GC_EXPANDERS_H__
#define __AFC_LANGUAGE_GC_EXPANDERS_H__

#include <map>
#include <memory>

#include "src/concurrent/protected.h"
#include "src/language/gc.h"
#include "src/language/gc_concepts.h"
#include "src/language/gc_view.h"
#include "src/language/safe_types.h"

namespace afc::language::gc {
template <typename V>
std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand(
    const NonNull<
        std::shared_ptr<concurrent::Protected<std::vector<gc::Ptr<V>>>>>&
        input) {
  return input->lock([](const std::vector<gc::Ptr<V>>& contents) {
    return container::MaterializeVector(contents | gc::view::ObjectMetadata);
  });
}

// Map must be any kind of map where the values are gc::Ptr<T>.
//
// Example: my_map | ExpandMapPtrValues | std::ranges::to<std::vector>()
struct ExpandMapPtrValues_fn
    : std::ranges::range_adaptor_closure<ExpandMapPtrValues_fn> {
  template <std::ranges::input_range R>
    requires gc::IsGcPtr<typename std::ranges::range_value_t<R>::second_type>
  auto operator()(R&& values) const {
    return std::forward<R>(values) | std::views::values |
           gc::view::ObjectMetadata;
  }
};

inline constexpr ExpandMapPtrValues_fn ExpandMapPtrValues;

}  // namespace afc::language::gc
#endif