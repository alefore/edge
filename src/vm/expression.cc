#include "src/vm/expression.h"

using afc::language::Error;
using afc::language::Success;
using afc::language::ValueOrError;

namespace afc::vm {
ValueOrError<std::unordered_set<Type>> CombineReturnTypes(
    std::unordered_set<Type> a, std::unordered_set<Type> b) {
  if (a.empty()) return Success(b);
  if (b.empty()) return Success(a);
  if (a != b) {
    return Error(L"Incompatible return types found: `" + ToString(*a.cbegin()) +
                 L"` and `" + ToString(*b.cbegin()) + L"`.");
  }
  return Success(a);
}
}  // namespace afc::vm
