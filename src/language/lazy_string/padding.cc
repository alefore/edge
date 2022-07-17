#include "src/language/lazy_string/padding.h"

#include "src/language/lazy_string/char_buffer.h"

namespace afc::language::lazy_string {
NonNull<std::shared_ptr<LazyString>> Padding(const ColumnNumberDelta& length,
                                             wchar_t fill) {
  CHECK_GE(length, ColumnNumberDelta(0));
  return NewLazyString(length, fill);
}
}  // namespace afc::language::lazy_string
