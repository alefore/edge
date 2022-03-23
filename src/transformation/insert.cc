#include "src/transformation/insert.h"

#include "src/char_buffer.h"
#include "src/server.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/vm_transformation.h"

namespace afc {
namespace vm {
template <>
struct VMTypeMapper<std::shared_ptr<editor::transformation::Insert>> {
  static std::shared_ptr<editor::transformation::Insert> get(Value* value) {
    CHECK(value != nullptr);
    CHECK(value->type.type == VMType::OBJECT_TYPE);
    CHECK(value->type.object_type == L"InsertTransformationBuilder");
    CHECK(value->user_value != nullptr);
    return std::static_pointer_cast<editor::transformation::Insert>(
        value->user_value);
  }
  static Value::Ptr New(std::shared_ptr<editor::transformation::Insert> value) {
    return Value::NewObject(L"InsertTransformationBuilder",
                            std::shared_ptr<void>(value, value.get()));
  }
  static const VMType vmtype;
};

const VMType
    VMTypeMapper<std::shared_ptr<editor::transformation::Insert>>::vmtype =
        VMType::ObjectType(L"InsertTransformationBuilder");
}  // namespace vm
namespace editor::transformation {
transformation::Delete GetCharactersDeleteOptions(size_t repetitions) {
  return transformation::Delete{
      .modifiers = {
          .repetitions = repetitions,
          .delete_behavior = Modifiers::DeleteBehavior::kDeleteText,
          .paste_buffer_behavior = Modifiers::PasteBufferBehavior::kDoNothing}};
}

futures::Value<transformation::Result> ApplyBase(const Insert& options,
                                                 transformation::Input input) {
  CHECK(input.buffer != nullptr);
  size_t length = options.buffer_to_insert->contents().CountCharacters();
  if (length == 0) {
    return futures::Past(transformation::Result(input.position));
  }

  auto result =
      std::make_shared<transformation::Result>(input.buffer->AdjustLineColumn(
          options.position.value_or(input.position)));

  result->modified_buffer = true;
  result->made_progress = true;

  LineColumn start_position = result->position;
  for (size_t i = 0; i < options.modifiers.repetitions.value_or(1); i++) {
    result->position = input.buffer->InsertInPosition(
        *options.buffer_to_insert, result->position, options.modifiers_set);
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
            CppEscapeString(
                options.buffer_to_insert->LineAt(LineNumber(0))->ToString()) +
            L")";
  output += L".set_modifiers(" + options.modifiers.Serialize() + L")";
  if (options.position.has_value()) {
    output += L".set_position(" + options.position.value().Serialize() + L")";
  }
  return output;
}

Insert OptimizeBase(Insert transformation) { return transformation; }

void RegisterInsert(EditorState* editor, vm::Environment* environment) {
  auto builder = std::make_unique<ObjectType>(L"InsertTransformationBuilder");

  environment->Define(L"InsertTransformationBuilder",
                      vm::NewCallback(std::function<std::shared_ptr<Insert>()>(
                          [] { return std::make_shared<Insert>(); })));

  builder->AddField(
      L"set_text",
      vm::NewCallback([editor](std::shared_ptr<Insert> options, wstring text) {
        CHECK(options != nullptr);
        auto buffer_to_insert = OpenBuffer::New(
            {.editor = *editor, .name = BufferName::TextInsertion()});
        if (!text.empty()) {
          buffer_to_insert->AppendLazyString(NewLazyString(std::move(text)));
          buffer_to_insert->EraseLines(LineNumber(0), LineNumber(1));
        }
        options->buffer_to_insert = buffer_to_insert;
        return options;
      }));

  builder->AddField(
      L"set_modifiers",
      vm::NewCallback([editor](std::shared_ptr<Insert> options,
                               std::shared_ptr<Modifiers> modifiers) {
        CHECK(options != nullptr);
        CHECK(modifiers != nullptr);

        options->modifiers = *modifiers;
        return options;
      }));

  builder->AddField(L"set_position",
                    NewCallback([editor](std::shared_ptr<Insert> options,
                                         LineColumn position) {
                      CHECK(options != nullptr);
                      options->position = position;
                      return options;
                    }));

  builder->AddField(L"build",
                    NewCallback([editor](std::shared_ptr<Insert> options) {
                      return std::make_unique<Variant>(*options).release();
                    }));

  environment->DefineType(L"InsertTransformationBuilder", std::move(builder));
}
}  // namespace editor::transformation
}  // namespace afc
