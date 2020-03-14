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
      vm::NewCallback(std::function<Transformation*(int)>([](int column) {
        return Build(SetPosition(ColumnNumber(column))).release();
      })));

  environment->Define(
      L"SetPositionTransformation",
      vm::NewCallback(
          std::function<Transformation*(LineColumn)>([](LineColumn position) {
            return Build(SetPosition(position)).release();
          })));
}

futures::Value<Transformation::Result> ApplyBase(const SetPosition& parameters,
                                                 Transformation::Input input) {
  Transformation::Result result(LineColumn(
      parameters.line.value_or(input.position.line), parameters.column));
  SetPosition undo_position(input.position.column);
  if (parameters.line.has_value()) {
    undo_position.line = input.position.line;
  }
  result.undo_stack->PushFront(Build(undo_position));
  result.made_progress = result.position != input.position;
  return futures::Past(std::move(result));
}
}  // namespace afc::editor::transformation
