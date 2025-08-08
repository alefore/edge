#include "src/vm/delegating_expression.h"

#include <unordered_set>
#include <utility>
#include <vector>

#include "src/futures/futures.h"
#include "src/language/error/log.h"
#include "src/language/overload.h"
#include "src/vm/compilation.h"
#include "src/vm/expression.h"
#include "src/vm/types.h"

using afc::futures::ValueOrError;
using afc::language::Error;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::ValueOrDie;
using afc::language::VisitOptional;
using afc::language::VisitPointer;
using afc::language::gc::ObjectMetadata;
using afc::vm::EvaluationOutput;
using afc::vm::Trampoline;
using afc::vm::Type;

namespace gc = afc::language::gc;

namespace afc::vm {
std::optional<gc::Ptr<Expression>> OptionalRootToPtr(
    const std::optional<gc::Root<Expression>>& input) {
  return VisitOptional(
      [](const gc::Root<Expression>& input_root) {
        return std::optional<gc::Ptr<Expression>>(input_root.ptr());
      },
      [] { return std::optional<gc::Ptr<Expression>>(); }, input);
}
}  // namespace afc::vm
