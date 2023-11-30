#ifndef __AFC_LANGUAGE_GC_CONTAINER_H__
#define __AFC_LANGUAGE_GC_CONTAINER_H__

#include <vector>

#include "src/language/container.h"
#include "src/language/gc.h"
#include "src/language/gc_view.h"

namespace afc::language::gc {
template <typename T>
struct ExpandHelper<std::vector<gc::Ptr<T>>> {
  std::vector<NonNull<std::shared_ptr<ObjectMetadata>>> operator()(
      const std::vector<gc::Ptr<T>>& input) {
    return container::MaterializeVector(input | gc::view::ObjectMetadata);
  }
};

}  // namespace afc::language::gc
#endif  // __AFC_LANGUAGE_GC_CONTAINER_H__
