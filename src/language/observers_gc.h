#ifndef __AFC_EDITOR_OBSERVERS_GC_H__
#define __AFC_EDITOR_OBSERVERS_GC_H__

#include "src/concurrent/protected.h"
#include "src/language/gc.h"
#include "src/language/observers.h"

namespace afc::language {
template <typename P, typename Callable>
static Observers::Observer WeakPtrLockingObserver(language::gc::WeakPtr<P> data,
                                                  Callable callable) {
  return [data, callable] {
    return VisitPointer(
        data.Lock(),
        [callable](language::gc::Root<P> root) {
          callable(root.ptr().value());
          return Observers::State::kAlive;
        },
        [] { return Observers::State::kExpired; });
  };
}
}  // namespace afc::language
#endif  // __AFC_EDITOR_OBSERVERS_GC_H__
