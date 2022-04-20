#include "src/lazy_string_functional.h"

namespace std {
std::size_t hash<afc::editor::LazyString>::operator()(
    const afc::editor::LazyString& input) const {
  size_t value = 0;
  afc::editor::ForEachColumn(input, [&](afc::editor::ColumnNumber, wchar_t c) {
    value = afc::language::hash_combine(value, c);
  });
  return value;
}
}  // namespace std
