#include "src/language/lazy_string/append.h"

#include <glog/logging.h>

#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
LazyString Append(LazyString a, LazyString b) { return a.Append(b); }

LazyString Append(LazyString a, LazyString b, LazyString c) {
  return Append(std::move(a), Append(std::move(b), std::move(c)));
}

LazyString Append(LazyString a, LazyString b, LazyString c, LazyString d) {
  return Append(Append(std::move(a), std::move(b)),
                Append(std::move(c), std::move(d)));
}
}  // namespace afc::language::lazy_string
