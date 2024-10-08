#include "src/transformation/insert.h"

#include "src/language/lazy_string/char_buffer.h"
#include "src/language/text/line_column_vm.h"
#include "src/language/text/mutable_line_sequence.h"
#include "src/transformation.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/transformation/vm.h"
#include "src/vm/escape.h"

namespace gc = afc::language::gc;

using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;

namespace afc {
namespace vm {
template <>
const types::ObjectName VMTypeMapper<NonNull<
    std::shared_ptr<editor::transformation::Insert>>>::object_type_name =
    types::ObjectName{Identifier{
        NON_EMPTY_SINGLE_LINE_CONSTANT(L"InsertTransformationBuilder")}};
}  // namespace vm
namespace editor::transformation {
using language::MakeNonNullShared;
using language::MakeNonNullUnique;

transformation::Delete GetCharactersDeleteOptions(size_t repetitions) {
  return transformation::Delete{
      .modifiers = {.repetitions = repetitions,
                    .paste_buffer_behavior =
                        Modifiers::PasteBufferBehavior::kDoNothing},
      .initiator = transformation::Delete::Initiator::kInternal};
}

futures::Value<transformation::Result> ApplyBase(const Insert& options,
                                                 transformation::Input input) {
  size_t length = options.contents_to_insert.CountCharacters();
  if (length == 0) {
    return futures::Past(transformation::Result(input.position));
  }

  auto result = std::make_shared<transformation::Result>(
      input.adapter.contents().AdjustLineColumn(
          options.position.value_or(input.position)));

  result->modified_buffer = true;
  result->made_progress = true;

  LineColumn start_position = result->position;
  for (size_t i = 0; i < options.modifiers.repetitions.value_or(1); i++) {
    result->position = input.adapter.InsertInPosition(
        options.contents_to_insert, result->position, options.modifiers_set);
  }
  LineColumn final_position = result->position;

  size_t chars_inserted = length * options.modifiers.repetitions.value_or(1);
  result->undo_stack->push_front(transformation::SetPosition(input.position));
  result->undo_stack->push_front(TransformationAtPosition(
      start_position, GetCharactersDeleteOptions(chars_inserted)));

  auto delayed_shared_result = futures::Past(result);
  if (options.modifiers.insertion == Modifiers::ModifyMode::kOverwrite) {
    transformation::Delete delete_options =
        GetCharactersDeleteOptions(chars_inserted);
    delete_options.line_end_behavior =
        transformation::Delete::LineEndBehavior::kStop;
    delayed_shared_result =
        Apply(TransformationAtPosition(result->position,
                                       std::move(delete_options)),
              input)
            .Transform([result](transformation::Result inner_result) {
              result->MergeFrom(std::move(inner_result));
              return result;
            });
  }

  LineColumn position = options.position.value_or(
      options.final_position == Insert::FinalPosition::kStart ? start_position
                                                              : final_position);

  return std::move(delayed_shared_result)
      .Transform(
          [position](std::shared_ptr<transformation::Result> final_result) {
            final_result->position = position;
            return std::move(*final_result);
          });
}

std::wstring ToStringBase(const Insert& options) {
  LazyString output =
      LazyString{L"InsertTransformationBuilder()"} + LazyString{L".set_text("} +
      vm::EscapedString::FromString(
          options.contents_to_insert.at(LineNumber(0)).contents().read())
          .CppRepresentation()
          .read() +
      LazyString{L")"} + LazyString{L".set_modifiers("} +
      LazyString{options.modifiers.Serialize()} + LazyString{L")"};
  if (options.position.has_value()) {
    output += LazyString{L".set_position("} +
              LazyString{options.position.value().Serialize()} +
              LazyString{L")"};
  }
  return output.ToString();
}

Insert OptimizeBase(Insert transformation) { return transformation; }

void RegisterInsert(gc::Pool& pool, vm::Environment& environment) {
  using vm::Identifier;
  using vm::ObjectType;
  using vm::PurityType;
  using vm::VMTypeMapper;
  gc::Root<ObjectType> builder = ObjectType::New(
      pool, VMTypeMapper<NonNull<std::shared_ptr<Insert>>>::object_type_name);
  environment.Define(
      VMTypeMapper<NonNull<std::shared_ptr<Insert>>>::object_type_name.read(),
      vm::NewCallback(pool, PurityType{}, MakeNonNullShared<Insert>));

  builder.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"set_text"}}}},
      vm::NewCallback(
          pool, vm::PurityType{.writes_external_outputs = true},
          [](NonNull<std::shared_ptr<Insert>> options, LazyString text) {
            options->contents_to_insert =
                LineSequence::BreakLines(std::move(text));
            return options;
          })
          .ptr());

  builder.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"set_modifiers"}}}},
      vm::NewCallback(pool, vm::PurityType{.writes_external_outputs = true},
                      [](NonNull<std::shared_ptr<Insert>> options,
                         NonNull<std::shared_ptr<Modifiers>> modifiers) {
                        options->modifiers = modifiers.value();
                        return options;
                      })
          .ptr());

  builder.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"set_position"}}}},
      NewCallback(
          pool, vm::PurityType{.writes_external_outputs = true},
          [](NonNull<std::shared_ptr<Insert>> options, LineColumn position) {
            options->position = position;
            return options;
          })
          .ptr());

  builder.ptr()->AddField(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"build"}}}},
      NewCallback(pool, PurityType{},
                  [](NonNull<std::shared_ptr<Insert>> options) {
                    return MakeNonNullShared<Variant>(options.value());
                  })
          .ptr());

  environment.DefineType(builder.ptr());
}
}  // namespace editor::transformation
}  // namespace afc
