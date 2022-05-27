#include "src/transformation/insert.h"

#include "src/buffer.h"
#include "src/char_buffer.h"
#include "src/editor.h"
#include "src/line_column_vm.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/transformation/vm.h"

namespace afc {
using language::NonNull;

namespace gc = language::gc;
namespace vm {
template <>
struct VMTypeMapper<std::shared_ptr<editor::transformation::Insert>> {
  static std::shared_ptr<editor::transformation::Insert> get(Value& value) {
    // TODO(easy, 2022-05-27): Drop get_shared below.
    return value.get_user_value<editor::transformation::Insert>(vmtype)
        .get_shared();
  }

  static gc::Root<Value> New(
      gc::Pool& pool, std::shared_ptr<editor::transformation::Insert> value) {
    // TODO(2022-05-27, easy): Receive `value` as NonNull.
    return Value::NewObject(
        pool, vmtype.object_type,
        NonNull<std::shared_ptr<editor::transformation::Insert>>::Unsafe(
            value));
  }

  static const VMType vmtype;
};

const VMType
    VMTypeMapper<std::shared_ptr<editor::transformation::Insert>>::vmtype =
        VMType::ObjectType(
            VMTypeObjectTypeName(L"InsertTransformationBuilder"));
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
  size_t length = options.contents_to_insert->CountCharacters();
  if (length == 0) {
    return futures::Past(transformation::Result(input.position));
  }

  auto result = std::make_shared<transformation::Result>(
      input.buffer.AdjustLineColumn(options.position.value_or(input.position)));

  result->modified_buffer = true;
  result->made_progress = true;

  LineColumn start_position = result->position;
  for (size_t i = 0; i < options.modifiers.repetitions.value_or(1); i++) {
    result->position =
        input.buffer.InsertInPosition(options.contents_to_insert.value(),
                                      result->position, options.modifiers_set);
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

  return delayed_shared_result.Transform(
      [position](std::shared_ptr<transformation::Result> result) {
        result->position = position;
        return std::move(*result);
      });
}

std::wstring ToStringBase(const Insert& options) {
  std::wstring output = L"InsertTransformationBuilder()";
  output += L".set_text(" +
            vm::CppEscapeString(
                options.contents_to_insert->at(LineNumber(0))->ToString()) +
            L")";
  output += L".set_modifiers(" + options.modifiers.Serialize() + L")";
  if (options.position.has_value()) {
    output += L".set_position(" + options.position.value().Serialize() + L")";
  }
  return output;
}

Insert OptimizeBase(Insert transformation) { return transformation; }

void RegisterInsert(gc::Pool& pool, vm::Environment& environment) {
  using vm::ObjectType;
  using vm::PurityType;
  using vm::VMTypeMapper;
  auto builder = MakeNonNullUnique<ObjectType>(
      VMTypeMapper<std::shared_ptr<Insert>>::vmtype);
  environment.Define(
      builder->type().object_type.read(),
      vm::NewCallback(pool, PurityType::kPure,
                      std::function<std::shared_ptr<Insert>()>(
                          [] { return std::make_shared<Insert>(); })));

  builder->AddField(
      L"set_text",
      vm::NewCallback(
          pool, vm::PurityTypeWriter,
          [](std::shared_ptr<Insert> options, std::wstring text) {
            CHECK(options != nullptr);
            NonNull<std::shared_ptr<BufferContents>> buffer;
            ColumnNumber line_start;
            for (ColumnNumber i; i.ToDelta() < ColumnNumberDelta(text.size());
                 ++i) {
              if (text[i.column] == L'\n') {
                VLOG(8) << "Adding line from " << line_start << " to " << i;
                buffer->push_back(MakeNonNullShared<Line>(
                    text.substr(line_start.column,
                                (ColumnNumber(i) - line_start).column_delta)));
                line_start = ColumnNumber(i) + ColumnNumberDelta(1);
              }
            }
            buffer->push_back(
                MakeNonNullShared<Line>(text.substr(line_start.column)));
            buffer->EraseLines(LineNumber(), LineNumber(1),
                               BufferContents::CursorsBehavior::kUnmodified);
            options->contents_to_insert = std::move(buffer);
            return options;
          }));

  builder->AddField(
      L"set_modifiers",
      vm::NewCallback(pool, vm::PurityTypeWriter,
                      [](std::shared_ptr<Insert> options,
                         NonNull<std::shared_ptr<Modifiers>> modifiers) {
                        CHECK(options != nullptr);
                        options->modifiers = modifiers.value();
                        return options;
                      }));

  builder->AddField(
      L"set_position",
      NewCallback(pool, vm::PurityTypeWriter,
                  [](std::shared_ptr<Insert> options, LineColumn position) {
                    CHECK(options != nullptr);
                    options->position = position;
                    return options;
                  }));

  builder->AddField(
      L"build",
      NewCallback(pool, PurityType::kPure, [](std::shared_ptr<Insert> options) {
        return MakeNonNullUnique<Variant>(*options);
      }));

  environment.DefineType(std::move(builder));
}
}  // namespace editor::transformation
}  // namespace afc
