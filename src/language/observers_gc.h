#ifndef __AFC_EDITOR_OBSERVERS_GC_H__
#define __AFC_EDITOR_OBSERVERS_GC_H__

#include "src/concurrent/protected.h"
#include "src/language/gc.h"
#include "src/language/observers.h"

namespace afc::language {
template <typename P, typename Callable>
static Observers::Observer WeakPtrLockingObserver(
    Callable&& callable, language::gc::WeakPtr<P> data) {
  return [data, callable = std::forward<Callable>(
                    callable)] mutable -> Observers::State {
    return VisitPointer(
        data.Lock(),
        [&callable](language::gc::Root<P> root) mutable {
          return std::forward<Callable>(callable)(root.ptr().value());
        },
        [] { return Observers::State::Expired; });
  };
}
}  // namespace afc::language
#endif  // __AFC_EDITOR_OBSERVERS_GC_H__
