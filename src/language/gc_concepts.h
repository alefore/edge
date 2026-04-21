#ifndef __AFC_LANGUAGE_GC_CONCEPTS_H__
#define __AFC_LANGUAGE_GC_CONCEPTS_H__

#include "src/language/gc.h"

namespace afc::language::gc {
template <typename T>
struct is_gc_ptr : std::false_type {};

template <typename T>
struct is_gc_ptr<Ptr<T>> : std::true_type {};

// 3. The Concept
template <typename T>
concept IsGcPtr = is_gc_ptr<std::remove_cvref_t<T>>::value;
}  // namespace afc::language::gc
#endif
