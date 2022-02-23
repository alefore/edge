#include "src/lazy_string_functional.h"

namespace afc::editor {
size_t Hash(const LazyString& input) {
  size_t value = 0;
  ForEachColumn(
      input, [&](ColumnNumber, wchar_t c) { value = hash_combine(value, c); });
  return value;
}
}  // namespace afc::editor
