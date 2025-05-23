#include "src/transformation/composite.h"

#include "src/buffer.h"
#include "src/editor.h"
#include "src/language/text/line_column_vm.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/transformation/vm.h"

namespace gc = afc::language::gc;

using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::text::LineColumn;
using afc::vm::kPurityTypePure;
using afc::vm::kPurityTypeUnknown;

namespace afc {
namespace vm {
template <>
const types::ObjectName VMTypeMapper<NonNull<std::shared_ptr<
    editor::CompositeTransformation::Output>>>::object_type_name =
    types::ObjectName{
        Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"TransformationOutput")}};

template <>
const types::ObjectName VMTypeMapper<NonNull<std::shared_ptr<
    editor::CompositeTransformation::Input>>>::object_type_name =
    types::ObjectName{
        Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"TransformationInput")}};
}  // namespace vm
namespace editor {
namespace transformation {
namespace {
futures::Value<Result> ApplyBase(const Modifiers& modifiers,
                                 CompositeTransformation& transformation,
                                 Input transformation_input) {
  NonNull<std::shared_ptr<Log>> trace =
      transformation_input.buffer.log().NewChild(
          LazyString{L"ApplyBase(CompositeTransformation)"});
  auto position = transformation_input.buffer.contents().AdjustLineColumn(
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
        return Apply(std::move(output.stack), transformation_input);
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

CompositeTransformation::Output::Output(Output&& other) : stack(other.stack) {}

CompositeTransformation::Output::Output(transformation::Variant transformation)
    : Output() {
  stack.push_back(std::move(transformation));
}

void CompositeTransformation::Output::Push(
    transformation::Variant transformation) {
  stack.push_back(std::move(transformation));
}

void RegisterCompositeTransformation(language::gc::Pool& pool,
                                     vm::Environment& environment) {
  using vm::ObjectType;
  using vm::PurityType;
  using vm::VMTypeMapper;

  gc::Root<ObjectType> input_type = ObjectType::New(
      pool, VMTypeMapper<NonNull<std::shared_ptr<
                editor::CompositeTransformation::Input>>>::object_type_name);

  input_type.ptr()->AddField(
      vm::Identifier{NonEmptySingleLine{SingleLine{LazyString{L"position"}}}},
      vm::NewCallback(
          pool, kPurityTypePure,
          [](NonNull<std::shared_ptr<CompositeTransformation::Input>> input) {
            return input->position;
          })
          .ptr());
  input_type.ptr()->AddField(
      vm::Identifier{NonEmptySingleLine{SingleLine{LazyString{L"range"}}}},
      vm::NewCallback(
          pool, kPurityTypePure,
          [](NonNull<std::shared_ptr<CompositeTransformation::Input>> input) {
            return input->range;
          })
          .ptr());

  input_type.ptr()->AddField(
      vm::Identifier{NonEmptySingleLine{SingleLine{LazyString{L"final_mode"}}}},
      vm::NewCallback(
          pool, kPurityTypePure,
          [](NonNull<std::shared_ptr<CompositeTransformation::Input>> input) {
            return input->mode == transformation::Input::Mode::kFinal;
          })
          .ptr());
  environment.DefineType(input_type.ptr());

  gc::Root<ObjectType> output_type = ObjectType::New(
      pool, VMTypeMapper<NonNull<std::shared_ptr<
                editor::CompositeTransformation::Output>>>::object_type_name);

  environment.Define(
      VMTypeMapper<NonNull<std::shared_ptr<
          editor::CompositeTransformation::Output>>>::object_type_name.read(),
      vm::NewCallback(pool, kPurityTypePure,
                      MakeNonNullShared<CompositeTransformation::Output>));

  output_type.ptr()->AddField(
      vm::Identifier{NonEmptySingleLine{SingleLine{LazyString{L"push"}}}},
      vm::NewCallback(
          pool, kPurityTypeUnknown,
          [](NonNull<std::shared_ptr<CompositeTransformation::Output>> output,
             NonNull<std::shared_ptr<transformation::Variant>> transformation) {
            output->Push(transformation.value());
            return output;
          })
          .ptr());

  environment.DefineType(output_type.ptr());
}
}  // namespace editor
}  // namespace afc
