#include "src/infrastructure/extended_char.h"

namespace afc::infrastructure {
std::vector<ExtendedChar> VectorExtendedChar(const std::wstring& input) {
  return std::vector<ExtendedChar>(input.begin(), input.end());
}
}  // namespace afc::infrastructure
