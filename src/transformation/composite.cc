#include "src/transformation/composite.h"

#include "src/buffer.h"
#include "src/editor.h"
#include "src/line_column_vm.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/vm_transformation.h"

namespace afc {
using language::MakeNonNullUnique;
using language::NonNull;

namespace gc = language::gc;

namespace vm {
const VMType VMTypeMapper<
    std::shared_ptr<editor::CompositeTransformation::Output>>::vmtype =
    VMType::ObjectType(VMTypeObjectTypeName(L"TransformationOutput"));

std::shared_ptr<editor::CompositeTransformation::Output>
VMTypeMapper<std::shared_ptr<editor::CompositeTransformation::Output>>::get(
    Value& value) {
  return std::static_pointer_cast<editor::CompositeTransformation::Output>(
      value.get_user_value(vmtype));
}

gc::Root<Value>
VMTypeMapper<std::shared_ptr<editor::CompositeTransformation::Output>>::New(
    gc::Pool& pool,
    std::shared_ptr<editor::CompositeTransformation::Output> value) {
  CHECK(value != nullptr);
  return Value::NewObject(pool, vmtype.object_type,
                          std::shared_ptr<void>(value, value.get()));
}

const VMType VMTypeMapper<
    std::shared_ptr<editor::CompositeTransformation::Input>>::vmtype =
    VMType::ObjectType(VMTypeObjectTypeName(L"TransformationInput"));

std::shared_ptr<editor::CompositeTransformation::Input>
VMTypeMapper<std::shared_ptr<editor::CompositeTransformation::Input>>::get(
    Value& value) {
  return std::static_pointer_cast<editor::CompositeTransformation::Input>(
      value.get_user_value(vmtype));
}

gc::Root<Value>
VMTypeMapper<std::shared_ptr<editor::CompositeTransformation::Input>>::New(
    gc::Pool& pool,
    std::shared_ptr<editor::CompositeTransformation::Input> value) {
  CHECK(value != nullptr);
  return Value::NewObject(pool, vmtype.object_type,
                          std::shared_ptr<void>(value, value.get()));
}

}  // namespace vm
namespace editor {
namespace transformation {
namespace {
futures::Value<Result> ApplyBase(const Modifiers& modifiers,
                                 CompositeTransformation& transformation,
                                 Input transformation_input) {
  NonNull<std::shared_ptr<Log>> trace =
      transformation_input.buffer.log().NewChild(
          L"ApplyBase(CompositeTransformation)");
  auto position = transformation_input.buffer.AdjustLineColumn(
      transformation_input.position);
  return transformation
      .Apply(CompositeTransformation::Input{
          .editor = transformation_input.buffer.editor(),
          .original_position = transformation_input.position,
          .position = position,
          .range =
              transformation_input.buffer.FindPartialRange(modifiers, position),
          .buffer = transformation_input.buffer,
          .modifiers = modifiers,
          .mode = transformation_input.mode})
      .Transform([transformation_input, trace = std::move(trace)](
                     CompositeTransformation::Output output) {
        return Apply(std::move(*output.stack), transformation_input);
      });
}
}  // namespace

futures::Value<Result> ApplyBase(
    const NonNull<std::shared_ptr<CompositeTransformation>>& transformation,
    Input input) {
  return ApplyBase(editor::Modifiers(), transformation.value(),
                   std::move(input));
}

futures::Value<Result> ApplyBase(const ModifiersAndComposite& parameters,
                                 Input input) {
  return ApplyBase(parameters.modifiers, parameters.transformation.value(),
                   std::move(input));
}

std::wstring ToStringBase(const ModifiersAndComposite& transformation) {
  return L"ModifiersAndComposite(" + ToString(transformation.transformation) +
         L")";
}
std::wstring ToStringBase(
    const NonNull<std::shared_ptr<CompositeTransformation>>& transformation) {
  return L"CompositeTransformation(" + transformation->Serialize() + L")";
}

Variant OptimizeBase(ModifiersAndComposite transformation) {
  return transformation;
}

Variant OptimizeBase(
    const NonNull<std::shared_ptr<CompositeTransformation>>& transformation) {
  return transformation;
}
}  // namespace transformation

CompositeTransformation::Output CompositeTransformation::Output::SetPosition(
    LineColumn position) {
  return Output(transformation::SetPosition(position));
}

CompositeTransformation::Output CompositeTransformation::Output::SetColumn(
    ColumnNumber column) {
  return Output(transformation::SetPosition(column));
}

CompositeTransformation::Output::Output()
    : stack(std::make_unique<transformation::Stack>()) {}

CompositeTransformation::Output::Output(Output&& other)
    : stack(std::move(other.stack)) {}

CompositeTransformation::Output::Output(transformation::Variant transformation)
    : Output() {
  stack->PushBack(std::move(transformation));
}

void CompositeTransformation::Output::Push(
    transformation::Variant transformation) {
  stack->PushBack(std::move(transformation));
}

void RegisterCompositeTransformation(language::gc::Pool& pool,
                                     vm::Environment& environment) {
  auto input_type = MakeNonNullUnique<ObjectType>(
      VMTypeMapper<
          std::shared_ptr<editor::CompositeTransformation::Input>>::vmtype);

  input_type->AddField(
      L"position",
      vm::NewCallback(
          pool, [](std::shared_ptr<CompositeTransformation::Input> input) {
            return input->position;
          }));
  input_type->AddField(
      L"range",
      vm::NewCallback(
          pool, [](std::shared_ptr<CompositeTransformation::Input> input) {
            return input->range;
          }));

  input_type->AddField(
      L"final_mode",
      vm::NewCallback(
          pool, [](std::shared_ptr<CompositeTransformation::Input> input) {
            return input->mode == transformation::Input::Mode::kFinal;
          }));
  environment.DefineType(std::move(input_type));

  auto output_type = MakeNonNullUnique<ObjectType>(
      VMTypeMapper<
          std::shared_ptr<editor::CompositeTransformation::Output>>::vmtype);

  environment.Define(
      output_type->type().object_type.read(), vm::NewCallback(pool, [] {
        return std::make_shared<CompositeTransformation::Output>();
      }));

  output_type->AddField(
      L"push",
      vm::NewCallback(
          pool, [](std::shared_ptr<CompositeTransformation::Output> output,
                   transformation::Variant* transformation) {
            // TODO(2022-05-02, easy): Receive transformation as NonNull.
            output->Push(*transformation);
            return output;
          }));

  environment.DefineType(std::move(output_type));
}
}  // namespace editor
}  // namespace afc
