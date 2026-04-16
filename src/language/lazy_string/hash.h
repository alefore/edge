#include "src/language/lazy_string/functional.h"

namespace afc::language::lazy_string {
template <typename StringType>
uint64_t fnv1a(const StringType& text) {
  constexpr std::uint64_t FNV_OFFSET_BASIS = 14695981039346656037ull;
  constexpr std::uint64_t FNV_PRIME = 1099511628211ull;
  uint64_t hash = FNV_OFFSET_BASIS;
  ForEachColumn(text, [&hash](ColumnNumber, wchar_t c) {
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&c);
    for (std::size_t j = 0; j < sizeof(wchar_t); ++j) {
      hash ^= static_cast<std::uint64_t>(bytes[j]);
      hash *= FNV_PRIME;
    }
  });
  return hash;
}
}  // namespace afc::language::lazy_string
