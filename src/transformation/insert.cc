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

namespace afc {
using language::NonNull;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::text::Line;
using language::text::LineColumn;
using language::text::LineNumber;
using language::text::MutableLineSequence;

namespace gc = language::gc;
namespace vm {
template <>
const types::ObjectName VMTypeMapper<NonNull<
    std::shared_ptr<editor::transformation::Insert>>>::object_type_name =
    types::ObjectName(L"InsertTransformationBuilder");
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
  result->undo_stack->PushFront(transformation::SetPosition(input.position));
  result->undo_stack->PushFront(TransformationAtPosition(
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
  std::wstring output = L"InsertTransformationBuilder()";
  output += L".set_text(" +
            vm::EscapedString::FromString(
                options.contents_to_insert.at(LineNumber(0)).contents())
                .CppRepresentation() +
            L")";
  output += L".set_modifiers(" + options.modifiers.Serialize() + L")";
  if (options.position.has_value()) {
    output += L".set_position(" + options.position.value().Serialize() + L")";
  }
  return output;
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
      Identifier(
          VMTypeMapper<NonNull<std::shared_ptr<Insert>>>::object_type_name
              .read()),
      vm::NewCallback(pool, PurityType::kPure, MakeNonNullShared<Insert>));

  builder.ptr()->AddField(
      Identifier(L"set_text"),
      vm::NewCallback(
          pool, vm::PurityTypeWriter,
          [](NonNull<std::shared_ptr<Insert>> options, std::wstring text) {
            MutableLineSequence output;
            ColumnNumber line_start;
            for (ColumnNumber i; i.ToDelta() < ColumnNumberDelta(text.size());
                 ++i) {
              if (text[i.read()] == L'\n') {
                VLOG(8) << "Adding line from " << line_start << " to " << i;
                output.push_back(Line(text.substr(
                    line_start.read(), (ColumnNumber(i) - line_start).read())));
                line_start = ColumnNumber(i) + ColumnNumberDelta(1);
              }
            }
            output.push_back(Line(text.substr(line_start.read())));
            output.EraseLines(LineNumber(), LineNumber(1),
                              MutableLineSequence::ObserverBehavior::kHide);
            options->contents_to_insert = output.snapshot();
            return options;
          })
          .ptr());

  builder.ptr()->AddField(
      Identifier(L"set_modifiers"),
      vm::NewCallback(pool, vm::PurityTypeWriter,
                      [](NonNull<std::shared_ptr<Insert>> options,
                         NonNull<std::shared_ptr<Modifiers>> modifiers) {
                        options->modifiers = modifiers.value();
                        return options;
                      })
          .ptr());

  builder.ptr()->AddField(
      Identifier(L"set_position"),
      NewCallback(
          pool, vm::PurityTypeWriter,
          [](NonNull<std::shared_ptr<Insert>> options, LineColumn position) {
            options->position = position;
            return options;
          })
          .ptr());

  builder.ptr()->AddField(
      Identifier(L"build"),
      NewCallback(pool, PurityType::kPure,
                  [](NonNull<std::shared_ptr<Insert>> options) {
                    return MakeNonNullShared<Variant>(options.value());
                  })
          .ptr());

  environment.DefineType(builder.ptr());
}
}  // namespace editor::transformation
}  // namespace afc
