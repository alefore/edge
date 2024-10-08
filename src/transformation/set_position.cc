#include "src/transformation/set_position.h"

#include "src/language/text/line_column_vm.h"
#include "src/transformation/composite.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/transformation/vm.h"

using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::text::LineColumn;
using afc::vm::Identifier;
using afc::vm::kPurityTypePure;

namespace afc::editor::transformation {
void RegisterSetPosition(language::gc::Pool& pool,
                         vm::Environment& environment) {
  using vm::PurityType;
  environment.Define(
      Identifier{NonEmptySingleLine{
          SingleLine{LazyString{L"SetColumnTransformation"}}}},
      vm::NewCallback(pool, kPurityTypePure, [](size_t column_number) {
        return MakeNonNullShared<Variant>(
            SetPosition(ColumnNumber(column_number)));
      }));

  environment.Define(
      Identifier{NonEmptySingleLine{
          SingleLine{LazyString{L"SetPositionTransformation"}}}},
      vm::NewCallback(pool, kPurityTypePure, [](LineColumn position) {
        return MakeNonNullShared<Variant>(SetPosition(position));
      }));
}

futures::Value<Result> ApplyBase(const SetPosition& parameters, Input input) {
  Result result(LineColumn(parameters.line.value_or(input.position.line),
                           parameters.column));
  SetPosition undo_position(input.position.column);
  if (parameters.line.has_value()) {
    undo_position.line = input.position.line;
  }
  result.undo_stack->push_front(std::move(undo_position));
  result.made_progress = result.position != input.position;
  return futures::Past(std::move(result));
}

std::wstring ToStringBase(const SetPosition& v) {
  if (v.line.has_value()) {
    return L"SetPositionTransformation(LineColumn(" +
           to_wstring(v.line.value()) + L", " + to_wstring(v.column) + L"))";
  }
  return L"SetColumnTransformation(" + to_wstring(v.column) + L")";
}

SetPosition OptimizeBase(SetPosition transformation) { return transformation; }
}  // namespace afc::editor::transformation
