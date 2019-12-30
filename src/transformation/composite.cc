#include "src/transformation/composite.h"

#include "src/buffer.h"
#include "src/editor.h"
#include "src/transformation/stack.h"

namespace afc::editor {
namespace {
class Adapter : public Transformation {
 public:
  Adapter(Modifiers modifiers,
          std::unique_ptr<CompositeTransformation> composite_transformation)
      : modifiers_(std::move(modifiers)),
        composite_transformation_(std::move(composite_transformation)) {}

  Result Apply(const Input& transformation_input) const override {
    TransformationStack stack;
    CompositeTransformation::Input input;
    input.buffer = transformation_input.buffer;
    input.original_position = transformation_input.position;
    input.position = input.buffer->AdjustLineColumn(input.original_position);
    input.editor = input.buffer->editor();
    input.mode = transformation_input.mode;
    input.range = transformation_input.buffer->FindPartialRange(modifiers_,
                                                                input.position);
    input.modifiers = modifiers_;
    input.push = [&](std::unique_ptr<Transformation> transformation) {
      stack.PushBack(std::move(transformation));
    };
    composite_transformation_->Apply(std::move(input));
    return stack.Apply(transformation_input);
  }

  std::unique_ptr<Transformation> Clone() const override {
    return std::make_unique<Adapter>(modifiers_,
                                     composite_transformation_->Clone());
  }

 private:
  const Modifiers modifiers_;
  const std::unique_ptr<CompositeTransformation> composite_transformation_;
};
}  // namespace

std::unique_ptr<Transformation> NewTransformation(
    Modifiers modifiers, std::unique_ptr<CompositeTransformation> composite) {
  return std::make_unique<Adapter>(std::move(modifiers), std::move(composite));
}
}  // namespace afc::editor
