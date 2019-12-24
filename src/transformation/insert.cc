#include "src/transformation/insert.h"

#include "src/transformation/delete.h"
#include "src/transformation/stack.h"
#include "src/vm_transformation.h"

namespace afc::editor {
namespace {
class InsertBufferTransformation : public Transformation {
 public:
  InsertBufferTransformation(InsertOptions options)
      : InsertBufferTransformation(
            std::move(options),
            options_.buffer_to_insert->contents()->CountCharacters()) {}

  InsertBufferTransformation(InsertOptions options,
                             size_t buffer_to_insert_length)
      : options_(std::move(options)),
        buffer_to_insert_length_(buffer_to_insert_length) {
    CHECK(options_.buffer_to_insert != nullptr);
  }

  void Apply(Result* result) const override {
    CHECK(result != nullptr);
    CHECK(result->buffer != nullptr);
    LineColumn position = options_.position.has_value()
                              ? options_.position.value()
                              : result->cursor;
    result->buffer->AdjustLineColumn(&position);
    LineColumn start_position = position;
    for (size_t i = 0; i < options_.modifiers.repetitions; i++) {
      position = result->buffer->InsertInPosition(
          *options_.buffer_to_insert, position, options_.modifiers_set);
    }

    if (!options_.position.has_value()) {
      result->cursor = position;
    }

    size_t chars_inserted =
        buffer_to_insert_length_ * options_.modifiers.repetitions;
    result->undo_stack->PushFront(TransformationAtPosition(
        start_position,
        NewDeleteTransformation(GetCharactersDeleteOptions(chars_inserted))));

    if (options_.modifiers.insertion == Modifiers::REPLACE) {
      Result current_result(result->buffer);
      DeleteOptions delete_options = GetCharactersDeleteOptions(chars_inserted);
      delete_options.line_end_behavior = DeleteOptions::LineEndBehavior::kStop;
      TransformationAtPosition(
          position, NewDeleteTransformation(std::move(delete_options)))
          ->Apply(&current_result);
      result->undo_stack->PushFront(std::move(current_result.undo_stack));
    }

    if (options_.final_position == InsertOptions::FinalPosition::kStart &&
        !options_.position.has_value()) {
      result->cursor = start_position;
    }

    result->modified_buffer = true;
    result->made_progress = true;
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

std::unique_ptr<Transformation> NewInsertBufferTransformation(
    InsertOptions insert_options) {
  return std::make_unique<InsertBufferTransformation>(
      std::move(insert_options));
}
}  // namespace afc::editor
