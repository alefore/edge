#include "src/transformation/set_position.h"

#include "src/transformation.h"
#include "src/transformation/composite.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/vm_transformation.h"

namespace afc::editor::transformation {
void RegisterSetPosition(vm::Environment* environment) {
  environment->Define(
      L"SetColumnTransformation",
      vm::NewCallback(std::function<Variant*(int)>([](int column) {
        return std::make_unique<Variant>(SetPosition(ColumnNumber(column)))
            .release();
      })));

  environment->Define(
      L"SetPositionTransformation",
      vm::NewCallback(
          std::function<Variant*(LineColumn)>([](LineColumn position) {
            return std::make_unique<Variant>(SetPosition(position)).release();
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
