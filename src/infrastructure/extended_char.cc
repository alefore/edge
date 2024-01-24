#include "src/infrastructure/extended_char.h"

using afc::language::lazy_string::LazyString;

namespace afc::infrastructure {
std::vector<ExtendedChar> VectorExtendedChar(const std::wstring& input) {
  return std::vector<ExtendedChar>(input.begin(), input.end());
}

std::vector<ExtendedChar> VectorExtendedChar(const LazyString& input) {
  return VectorExtendedChar(input.ToString());
}

}  // namespace afc::infrastructure
