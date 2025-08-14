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
language::ValueOrError<language::gc::Ptr<Expression>> ToPtr(
    const RootExpressionOrError& input) {
  if (IsError(input)) return std::get<Error>(input);
  return std::get<gc::Root<Expression>>(input).ptr();
}

std::optional<gc::Ptr<Expression>> OptionalRootToPtr(
    const std::optional<gc::Root<Expression>>& input) {
  return VisitOptional(
      [](const gc::Root<Expression>& input_root) {
        return std::optional<gc::Ptr<Expression>>(input_root.ptr());
      },
      [] { return std::optional<gc::Ptr<Expression>>(); }, input);
}

RootExpressionOrError Pop(RootExpressionOrError* value_raw) {
  std::unique_ptr<RootExpressionOrError> value{value_raw};
  CHECK(value != nullptr);
  return std::move(*value);
}
}  // namespace afc::vm
