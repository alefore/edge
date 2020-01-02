#include "src/transformation/insert.h"

#include "src/char_buffer.h"
#include "src/server.h"
#include "src/transformation/delete.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/vm_transformation.h"

namespace afc {
namespace vm {
template <>
struct VMTypeMapper<std::shared_ptr<editor::InsertOptions>> {
  static std::shared_ptr<editor::InsertOptions> get(Value* value) {
    CHECK(value != nullptr);
    CHECK(value->type.type == VMType::OBJECT_TYPE);
    CHECK(value->type.object_type == L"InsertTransformationBuilder");
    CHECK(value->user_value != nullptr);
    return std::static_pointer_cast<editor::InsertOptions>(value->user_value);
  }
  static Value::Ptr New(std::shared_ptr<editor::InsertOptions> value) {
    return Value::NewObject(L"InsertTransformationBuilder",
                            std::shared_ptr<void>(value, value.get()));
  }
  static const VMType vmtype;
};

const VMType VMTypeMapper<std::shared_ptr<editor::InsertOptions>>::vmtype =
    VMType::ObjectType(L"InsertTransformationBuilder");
}  // namespace vm
namespace editor {
namespace {
class InsertBufferTransformation : public Transformation {
 public:
  InsertBufferTransformation(InsertOptions options,
                             size_t buffer_to_insert_length)
      : options_(std::move(options)),
        buffer_to_insert_length_(buffer_to_insert_length) {
    CHECK(options_.buffer_to_insert != nullptr);
  }

  std::wstring Serialize() const { return options_.Serialize() + L".build()"; }

  DelayedValue<Result> Apply(const Input& input) const override {
    CHECK(input.buffer != nullptr);
    if (buffer_to_insert_length_ == 0) {
      return futures::ImmediateValue(Result(input.position));
    }

    auto result = std::make_shared<Result>(input.buffer->AdjustLineColumn(
        options_.position.value_or(input.position)));

    result->modified_buffer = true;
    result->made_progress = true;

    LineColumn start_position = result->position;
    for (size_t i = 0; i < options_.modifiers.repetitions; i++) {
      result->position = input.buffer->InsertInPosition(
          *options_.buffer_to_insert, result->position, options_.modifiers_set);
    }
    LineColumn final_position = result->position;

    size_t chars_inserted =
        buffer_to_insert_length_ * options_.modifiers.repetitions;
    result->undo_stack->PushFront(NewSetPositionTransformation(input.position));
    result->undo_stack->PushFront(TransformationAtPosition(
        start_position,
        NewDeleteTransformation(GetCharactersDeleteOptions(chars_inserted))));

    auto delayed_shared_result = futures::ImmediateValue(result);
    if (options_.modifiers.insertion == Modifiers::REPLACE) {
      DeleteOptions delete_options = GetCharactersDeleteOptions(chars_inserted);
      delete_options.line_end_behavior = DeleteOptions::LineEndBehavior::kStop;
      delete_options.copy_to_paste_buffer = false;
      delayed_shared_result =
          DelayedValue<std::shared_ptr<Transformation::Result>>::Transform(
              TransformationAtPosition(
                  result->position,
                  NewDeleteTransformation(std::move(delete_options)))
                  ->Apply(input),
              [result](const Transformation::Result& inner_result) {
                result->MergeFrom(inner_result);
                return futures::ImmediateValue(result);
              });
    }

    LineColumn position = options_.position.value_or(
        options_.final_position == InsertOptions::FinalPosition::kStart
            ? start_position
            : final_position);

    return DelayedValue<Transformation::Result>::Transform(
        delayed_shared_result,
        [position](const std::shared_ptr<Transformation::Result>& result) {
          result->position = position;
          return futures::ImmediateValue(std::move(*result));
        });
  }

  unique_ptr<Transformation> Clone() const override {
    return std::make_unique<InsertBufferTransformation>(
        options_, buffer_to_insert_length_);
  }

 private:
  static DeleteOptions GetCharactersDeleteOptions(size_t repetitions) {
    DeleteOptions output;
    output.modifiers.repetitions = repetitions;
    output.copy_to_paste_buffer = false;
    return output;
  }

  const InsertOptions options_;
  const size_t buffer_to_insert_length_;
};
}  // namespace

std::wstring InsertOptions::Serialize() const {
  std::wstring output = L"InsertTransformationBuilder()";
  output +=
      L".set_text(" +
      CppEscapeString(buffer_to_insert->LineAt(LineNumber(0))->ToString()) +
      L")";
  output += L".set_modifiers(" + modifiers.Serialize() + L")";
  if (position.has_value()) {
    output += L".set_position(" + position.value().Serialize() + L")";
  }
  return output;
}
std::unique_ptr<Transformation> NewInsertBufferTransformation(
    InsertOptions insert_options) {
  size_t buffer_to_insert_length =
      insert_options.buffer_to_insert->contents()->CountCharacters();
  return std::make_unique<InsertBufferTransformation>(std::move(insert_options),
                                                      buffer_to_insert_length);
}

void RegisterInsertTransformation(EditorState* editor,
                                  vm::Environment* environment) {
  auto builder = std::make_unique<ObjectType>(L"InsertTransformationBuilder");

  environment->Define(
      L"InsertTransformationBuilder",
      vm::NewCallback(std::function<std::shared_ptr<InsertOptions>()>(
          [] { return std::make_shared<InsertOptions>(); })));

  builder->AddField(
      L"set_text",
      vm::NewCallback(std::function<std::shared_ptr<InsertOptions>(
                          std::shared_ptr<InsertOptions>, wstring)>(
          [editor](std::shared_ptr<InsertOptions> options, wstring text) {
            CHECK(options != nullptr);
            auto buffer_to_insert =
                std::make_shared<OpenBuffer>(editor, L"- text inserted");
            if (!text.empty()) {
              buffer_to_insert->AppendLazyString(
                  NewLazyString(std::move(text)));
              buffer_to_insert->EraseLines(LineNumber(0), LineNumber(1));
            }
            options->buffer_to_insert = buffer_to_insert;
            return options;
          })));

  builder->AddField(
      L"set_modifiers",
      vm::NewCallback(
          std::function<std::shared_ptr<InsertOptions>(
              std::shared_ptr<InsertOptions>, std::shared_ptr<Modifiers>)>(
              [editor](std::shared_ptr<InsertOptions> options,
                       std::shared_ptr<Modifiers> modifiers) {
                CHECK(options != nullptr);
                CHECK(modifiers != nullptr);

                options->modifiers = *modifiers;
                return options;
              })));

  builder->AddField(
      L"set_position",
      NewCallback(std::function<std::shared_ptr<InsertOptions>(
                      std::shared_ptr<InsertOptions>, LineColumn)>(
          [editor](std::shared_ptr<InsertOptions> options,
                   LineColumn position) {
            CHECK(options != nullptr);
            options->position = position;
            return options;
          })));

  builder->AddField(
      L"build",
      NewCallback(
          std::function<Transformation*(std::shared_ptr<InsertOptions>)>(
              [editor](std::shared_ptr<InsertOptions> options) {
                return NewInsertBufferTransformation(*options).release();
              })));

  environment->DefineType(L"InsertTransformationBuilder", std::move(builder));
}
}  // namespace editor
}  // namespace afc
