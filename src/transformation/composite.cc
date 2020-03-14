#include "src/transformation/composite.h"

#include "src/buffer.h"
#include "src/editor.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
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
namespace transformation {
namespace {
futures::Value<Transformation::Result> ApplyBase(
    const Modifiers& modifiers, CompositeTransformation* transformation,
    Transformation::Input transformation_input) {
  CompositeTransformation::Input input;
  input.buffer = transformation_input.buffer;
  input.original_position = transformation_input.position;
  input.position = input.buffer->AdjustLineColumn(input.original_position);
  input.editor = input.buffer->editor();
  input.mode = transformation_input.mode;
  input.range =
      transformation_input.buffer->FindPartialRange(modifiers, input.position);
  input.modifiers = modifiers;
  return futures::Transform(
      transformation->Apply(std::move(input)),
      [transformation_input](CompositeTransformation::Output output) {
        return Build(std::move(*output.stack))->Apply(transformation_input);
      });
}
}  // namespace

futures::Value<Transformation::Result> ApplyBase(
    const std::shared_ptr<CompositeTransformation>& transformation,
    Transformation::Input input) {
  return ApplyBase(editor::Modifiers(), transformation.get(), std::move(input));
}

futures::Value<Transformation::Result> ApplyBase(
    const ModifiersAndComposite& parameters, Transformation::Input input) {
  return ApplyBase(parameters.modifiers, parameters.transformation.get(),
                   std::move(input));
}

}  // namespace transformation

CompositeTransformation::Output CompositeTransformation::Output::SetPosition(
    LineColumn position) {
  return Output(transformation::Build(transformation::SetPosition(position)));
}

CompositeTransformation::Output CompositeTransformation::Output::SetColumn(
    ColumnNumber column) {
  return Output(transformation::Build(transformation::SetPosition(column)));
}

CompositeTransformation::Output::Output()
    : stack(std::make_unique<transformation::Stack>()) {}

CompositeTransformation::Output::Output(Output&& other)
    : stack(std::move(other.stack)) {}

CompositeTransformation::Output::Output(
    std::unique_ptr<Transformation> transformation)
    : Output() {
  stack->PushBack(std::move(transformation));
}

void CompositeTransformation::Output::Push(
    std::unique_ptr<Transformation> transformation) {
  stack->PushBack(std::move(transformation));
}

void CompositeTransformation::Output::Push(
    transformation::BaseTransformation base_transformation) {
  return Push(transformation::Build(std::move(base_transformation)));
}

void RegisterCompositeTransformation(vm::Environment* environment) {
  auto input_type = std::make_unique<ObjectType>(L"TransformationInput");

  input_type->AddField(
      L"position",
      vm::NewCallback(
          [](std::shared_ptr<CompositeTransformation::Input> input) {
            return input->position;
          }));
  input_type->AddField(
      L"range", vm::NewCallback(
                    [](std::shared_ptr<CompositeTransformation::Input> input) {
                      return input->range;
                    }));

  input_type->AddField(
      L"final_mode",
      vm::NewCallback(
          [](std::shared_ptr<CompositeTransformation::Input> input) {
            return input->mode == Transformation::Input::Mode::kFinal;
          }));
  environment->DefineType(L"TransformationInput", std::move(input_type));

  auto output_type = std::make_unique<ObjectType>(L"TransformationOutput");
  environment->Define(
      L"TransformationOutput", vm::NewCallback([] {
        return std::make_shared<CompositeTransformation::Output>();
      }));

  output_type->AddField(
      L"push", vm::NewCallback(
                   [](std::shared_ptr<CompositeTransformation::Output> output,
                      Transformation* transformation) {
                     output->Push(transformation->Clone());
                     return output;
                   }));

  environment->DefineType(L"TransformationOutput", std::move(output_type));
}
}  // namespace editor
}  // namespace afc
