#include "src/language/lazy_string/functional.h"

namespace std {
std::size_t hash<afc::language::lazy_string::LazyString>::operator()(
    const afc::language::lazy_string::LazyString& input) const {
  size_t value = 0;
  afc::language::lazy_string::ForEachColumn(
      input, [&](afc::language::lazy_string::ColumnNumber, wchar_t c) {
        value = afc::language::hash_combine(value, c);
      });
  return value;
}
}  // namespace std
