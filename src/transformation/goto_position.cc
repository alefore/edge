#include "src/transformation/goto_position.h"

#include "src/transformation.h"
#include "src/vm/public/callbacks.h"
#include "src/vm_transformation.h"

namespace afc::editor {
namespace {
class GotoPositionTransformation : public Transformation {
 public:
  static void Register(vm::Environment* environment) {
    environment->Define(
        L"TransformationGoToColumn",
        vm::NewCallback(std::function<Transformation*(int)>([](int column) {
          return NewGotoPositionTransformation(std::nullopt,
                                               ColumnNumber(column))
              .release();
        })));

    environment->Define(
        L"TransformationGoToPosition",
        vm::NewCallback(
            std::function<Transformation*(LineColumn)>([](LineColumn position) {
              return NewGotoPositionTransformation(position.line,
                                                   position.column)
                  .release();
            })));
  }

  GotoPositionTransformation(std::optional<LineNumber> line,
                             ColumnNumber column)
      : line_(line), column_(column) {}

  void Apply(Result* result) const override {
    CHECK(result != nullptr);
    CHECK(result->buffer != nullptr);
    result->undo_stack->PushFront(NewGotoPositionTransformation(
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
    return NewGotoPositionTransformation(line_, column_);
  }

 private:
  const std::optional<LineNumber> line_;
  const ColumnNumber column_;
};
}  // namespace

// TODO: Get rid of this, just have everyone call the other directly.
std::unique_ptr<Transformation> NewGotoPositionTransformation(
    LineColumn position) {
  return std::make_unique<GotoPositionTransformation>(position.line,
                                                      position.column);
}

std::unique_ptr<Transformation> NewGotoPositionTransformation(
    std::optional<LineNumber> line, ColumnNumber column) {
  return std::make_unique<GotoPositionTransformation>(line, column);
}

void RegisterGotoPositionTransformation(vm::Environment* environment) {
  GotoPositionTransformation::Register(environment);
}
}  // namespace afc::editor
