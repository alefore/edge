#include "src/language/version_value.h"

namespace afc::language::staging {
bool operator==(const Clean_t&, const Clean_t&) { return true; }

Origin MergeOrigins(Origin a, Origin b) {
  Revision* a_ptr = std::get_if<Revision>(&a);
  Revision* b_ptr = std::get_if<Revision>(&b);
  if (!a_ptr) return b;
  if (!b_ptr) return a;
  return *a_ptr > *b_ptr ? a : b;
}

}  // namespace afc::language::staging
