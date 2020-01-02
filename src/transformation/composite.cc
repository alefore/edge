#include "src/transformation/composite.h"

#include "src/buffer.h"
#include "src/editor.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/vm_transformation.h"

namespace afc {
namespace vm {
const VMType VMTypeMapper<
    std::shared_ptr<editor::CompositeTransformation::Output>>::vmtype =
    VMType::ObjectType(L"TransformationOutput");

std::shared_ptr<editor::CompositeTransformation::Output>
VMTypeMapper<std::shared_ptr<editor::CompositeTransformation::Output>>::get(
    Value* value) {
  CHECK(value != nullptr);
  CHECK(value->type.type == VMType::OBJECT_TYPE);
  CHECK(value->type.object_type == L"TransformationOutput");
  CHECK(value->user_value != nullptr);
  return std::static_pointer_cast<editor::CompositeTransformation::Output>(
      value->user_value);
}

Value::Ptr
VMTypeMapper<std::shared_ptr<editor::CompositeTransformation::Output>>::New(
    std::shared_ptr<editor::CompositeTransformation::Output> value) {
  CHECK(value != nullptr);
  return Value::NewObject(L"TransformationOutput",
                          std::shared_ptr<void>(value, value.get()));
}

const VMType VMTypeMapper<
    std::shared_ptr<editor::CompositeTransformation::Input>>::vmtype =
    VMType::ObjectType(L"TransformationInput");

std::shared_ptr<editor::CompositeTransformation::Input>
VMTypeMapper<std::shared_ptr<editor::CompositeTransformation::Input>>::get(
    Value* value) {
  CHECK(value != nullptr);
  CHECK(value->type.type == VMType::OBJECT_TYPE);
  CHECK(value->type.object_type == L"TransformationInput");
  CHECK(value->user_value != nullptr);
  return std::static_pointer_cast<editor::CompositeTransformation::Input>(
      value->user_value);
}

Value::Ptr
VMTypeMapper<std::shared_ptr<editor::CompositeTransformation::Input>>::New(
    std::shared_ptr<editor::CompositeTransformation::Input> value) {
  CHECK(value != nullptr);
  return Value::NewObject(L"TransformationInput",
                          std::shared_ptr<void>(value, value.get()));
}

}  // namespace vm
namespace editor {
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
void RegisterCompositeTransformation(vm::Environment* environment) {
  auto input_type = std::make_unique<ObjectType>(L"TransformationInput");

  input_type->AddField(
      L"position",
      vm::NewCallback(std::function<LineColumn(
                          std::shared_ptr<CompositeTransformation::Input>)>(
          [](std::shared_ptr<CompositeTransformation::Input> input) {
            return input->position;
          })));
  environment->DefineType(L"TransformationInput", std::move(input_type));

  auto output_type = std::make_unique<ObjectType>(L"TransformationOutput");
  environment->Define(
      L"TransformationOutput",
      vm::NewCallback(
          std::function<std::shared_ptr<CompositeTransformation::Output>()>([] {
            return std::make_shared<CompositeTransformation::Output>();
          })));

  output_type->AddField(
      L"push",
      vm::NewCallback(
          std::function<std::shared_ptr<CompositeTransformation::Output>(
              std::shared_ptr<CompositeTransformation::Output>,
              Transformation*)>(
              [](std::shared_ptr<CompositeTransformation::Output> output,
                 Transformation* transformation) {
                output->Push(transformation->Clone());
                return output;
              })));

  environment->DefineType(L"TransformationOutput", std::move(output_type));
}
}  // namespace editor
}  // namespace afc
