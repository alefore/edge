#include "src/transformation/set_position.h"

#include "src/line_column_vm.h"
#include "src/transformation.h"
#include "src/transformation/composite.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/vm_transformation.h"

namespace afc::editor::transformation {
using language::MakeNonNullUnique;

void RegisterSetPosition(language::gc::Pool& pool,
                         vm::Environment* environment) {
  environment->Define(
      L"SetColumnTransformation",
      vm::NewCallback(pool, std::function<Variant*(int)>([](int column) {
                        return MakeNonNullUnique<Variant>(
                                   SetPosition(ColumnNumber(column)))
                            .get_unique()
                            .release();
                      })));

  environment->Define(
      L"SetPositionTransformation",
      vm::NewCallback(
          pool, std::function<Variant*(LineColumn)>([](LineColumn position) {
            return MakeNonNullUnique<Variant>(SetPosition(position))
                .get_unique()
                .release();
          })));
}

futures::Value<Result> ApplyBase(const SetPosition& parameters, Input input) {
  Result result(LineColumn(parameters.line.value_or(input.position.line),
                           parameters.column));
  SetPosition undo_position(input.position.column);
  if (parameters.line.has_value()) {
    undo_position.line = input.position.line;
  }
  result.undo_stack->PushFront(std::move(undo_position));
  result.made_progress = result.position != input.position;
  return futures::Past(std::move(result));
}

std::wstring ToStringBase(const SetPosition& v) {
  if (v.line.has_value()) {
    return L"SetPositionTransformation(LineColumn(" +
           std::to_wstring(v.line.value().line) + L", " +
           std::to_wstring(v.column.column) + L"))";
  }
  return L"SetColumnTransformation(" + std::to_wstring(v.column.column) + L")";
}

SetPosition OptimizeBase(SetPosition transformation) { return transformation; }
}  // namespace afc::editor::transformation
