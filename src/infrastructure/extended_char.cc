#include "src/infrastructure/extended_char.h"

#include "src/language/container.h"

using afc::language::lazy_string::LazyString;

namespace afc::infrastructure {
std::vector<ExtendedChar> VectorExtendedChar(const LazyString& input) {
  // Why spell the vector type explicitly? To trigger conversion from wchar_t to
  // ExtendedChar.
  return std::vector<ExtendedChar>(std::from_range, input);
}

}  // namespace afc::infrastructure
