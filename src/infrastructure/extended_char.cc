#include "src/infrastructure/extended_char.h"

#include "src/language/container.h"

using afc::language::lazy_string::LazyString;

namespace afc::infrastructure {
std::vector<ExtendedChar> VectorExtendedChar(const LazyString& input) {
  return language::container::Materialize<std::vector<ExtendedChar>>(input);
}

}  // namespace afc::infrastructure
