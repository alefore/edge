#include "src/transformation/set_position.h"

#include "src/transformation.h"
#include "src/vm/public/callbacks.h"
#include "src/vm_transformation.h"

namespace afc::editor {
namespace {
class SetPositionTransformation : public Transformation {
 public:
  static void Register(vm::Environment* environment) {
    // TODO: Rename `SetPosition` (rather than `GoToPosition`).
    environment->Define(
        L"TransformationGoToColumn",
        vm::NewCallback(std::function<Transformation*(int)>([](int column) {
          return NewSetPositionTransformation(std::nullopt,
                                              ColumnNumber(column))
              .release();
        })));

    // TODO: Rename `SetPosition` (rather than `GoToPosition`).
    environment->Define(
        L"TransformationGoToPosition",
        vm::NewCallback(
            std::function<Transformation*(LineColumn)>([](LineColumn position) {
              return NewSetPositionTransformation(position.line,
                                                  position.column)
                  .release();
            })));
  }

  SetPositionTransformation(std::optional<LineNumber> line, ColumnNumber column)
      : line_(line), column_(column) {}

  void Apply(Result* result) const override {
    CHECK(result != nullptr);
    CHECK(result->buffer != nullptr);
    result->undo_stack->PushFront(NewSetPositionTransformation(
        line_.has_value() ? std::optional<LineNumber>(result->cursor.line)
                          : std::nullopt,
        result->cursor.column));
    if (line_.has_value()) {
      result->cursor.line = line_.value();
    }
    result->cursor.column = column_;
    result->success = true;
  }

  std::unique_ptr<Transformation> Clone() const override {
    return NewSetPositionTransformation(line_, column_);
  }

 private:
  const std::optional<LineNumber> line_;
  const ColumnNumber column_;
};
}  // namespace

// TODO: Get rid of this, just have everyone call the other directly.
std::unique_ptr<Transformation> NewSetPositionTransformation(
    LineColumn position) {
  return std::make_unique<SetPositionTransformation>(position.line,
                                                     position.column);
}

std::unique_ptr<Transformation> NewSetPositionTransformation(
    std::optional<LineNumber> line, ColumnNumber column) {
  return std::make_unique<SetPositionTransformation>(line, column);
}

void RegisterSetPositionTransformation(vm::Environment* environment) {
  SetPositionTransformation::Register(environment);
}
}  // namespace afc::editor
