#include "src/transformation/composite.h"

#include "src/buffer.h"
#include "src/editor.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"

namespace afc::editor {
CompositeTransformationAdapter::CompositeTransformationAdapter(
    Modifiers modifiers,
    std::unique_ptr<CompositeTransformation> composite_transformation)
    : modifiers_(std::move(modifiers)),
      composite_transformation_(std::move(composite_transformation)) {}

futures::DelayedValue<Transformation::Result>
CompositeTransformationAdapter::Apply(const Input& transformation_input) const {
  CompositeTransformation::Input input;
  input.buffer = transformation_input.buffer;
  input.original_position = transformation_input.position;
  input.position = input.buffer->AdjustLineColumn(input.original_position);
  input.editor = input.buffer->editor();
  input.mode = transformation_input.mode;
  input.range =
      transformation_input.buffer->FindPartialRange(modifiers_, input.position);
  input.modifiers = modifiers_;
  return futures::DelayedValue<Transformation::Result>::Transform(
      composite_transformation_->Apply(std::move(input)),
      [transformation_input](const CompositeTransformation::Output& output) {
        return output.transformations_->Apply(transformation_input);
      });
}

std::unique_ptr<Transformation> CompositeTransformationAdapter::Clone() const {
  return std::make_unique<CompositeTransformationAdapter>(
      modifiers_, composite_transformation_->Clone());
}

CompositeTransformation::Output CompositeTransformation::Output::SetPosition(
    LineColumn position) {
  return Output(NewSetPositionTransformation(position));
}

CompositeTransformation::Output CompositeTransformation::Output::SetColumn(
    ColumnNumber column) {
  return Output(NewSetPositionTransformation(std::nullopt, column));
}

CompositeTransformation::Output::Output()
    : transformations_(std::make_unique<TransformationStack>()) {}

CompositeTransformation::Output::Output(
    std::unique_ptr<Transformation> transformation)
    : Output() {
  transformations_->PushBack(std::move(transformation));
}

void CompositeTransformation::Output::Push(
    std::unique_ptr<Transformation> transformation) {
  transformations_->PushBack(std::move(transformation));
}

std::unique_ptr<Transformation> NewTransformation(
    Modifiers modifiers, std::unique_ptr<CompositeTransformation> composite) {
  return std::make_unique<CompositeTransformationAdapter>(std::move(modifiers),
                                                          std::move(composite));
}
}  // namespace afc::editor
