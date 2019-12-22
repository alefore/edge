#include "src/transformation.h"

#include <glog/logging.h>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/lazy_string_append.h"
#include "src/transformation/delete.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"

namespace afc::editor {
namespace {
class InsertBufferTransformation : public Transformation {
 public:
  InsertBufferTransformation(InsertOptions options)
      : options_(std::move(options)),
        buffer_to_insert_length_(
            options_.buffer_to_insert->contents()->CountCharacters()) {
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
    DeleteOptions delete_options;
    delete_options.modifiers.repetitions = chars_inserted;
    delete_options.copy_to_paste_buffer = false;
    result->undo_stack->PushFront(TransformationAtPosition(
        start_position, NewDeleteTransformation(delete_options)));

    if (options_.modifiers.insertion == Modifiers::REPLACE) {
      Result current_result(result->buffer);
      DeleteOptions delete_options;
      delete_options.modifiers.repetitions = chars_inserted;
      delete_options.copy_to_paste_buffer = false;
      delete_options.line_end_behavior = DeleteOptions::LineEndBehavior::kStop;
      TransformationAtPosition(position,
                               NewDeleteTransformation(delete_options))
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
    return NewInsertBufferTransformation(options_);
  }

 private:
  InsertOptions options_;
  size_t buffer_to_insert_length_;
};

class DeleteSuffixSuperfluousCharacters : public Transformation {
 public:
  void Apply(Result* result) const override {
    CHECK(result != nullptr);
    CHECK(result->buffer != nullptr);
    const wstring& superfluous_characters(result->buffer->Read(
        buffer_variables::line_suffix_superfluous_characters));
    const auto line = result->buffer->LineAt(result->cursor.line);
    if (line == nullptr) {
      result->made_progress = false;
      return;
    }
    ColumnNumber column = line->EndColumn();
    while (column > ColumnNumber(0) &&
           superfluous_characters.find(
               line->get(column - ColumnNumberDelta(1))) != string::npos) {
      --column;
    }
    if (column == line->EndColumn()) {
      return;
    }
    CHECK_LT(column, line->EndColumn());
    DeleteOptions delete_options;
    delete_options.modifiers.repetitions =
        (line->EndColumn() - column).column_delta;
    delete_options.copy_to_paste_buffer = false;
    return TransformationAtPosition(LineColumn(result->cursor.line, column),
                                    NewDeleteTransformation(delete_options))
        ->Apply(result);
  }

  unique_ptr<Transformation> Clone() const override {
    return NewDeleteSuffixSuperfluousCharacters();
  }
};

class SetRepetitionsTransformation : public Transformation {
 public:
  SetRepetitionsTransformation(int repetitions,
                               unique_ptr<Transformation> delegate)
      : repetitions_(repetitions), delegate_(std::move(delegate)) {}

  void Apply(Result* result) const override {
    CHECK(result != nullptr);
    CHECK(result->buffer != nullptr);
    auto editor_state = result->buffer->editor();
    auto original_repetitions = editor_state->repetitions();
    editor_state->set_repetitions(repetitions_);
    delegate_->Apply(result);
    editor_state->set_repetitions(original_repetitions);
  }

  unique_ptr<Transformation> Clone() const override {
    return NewSetRepetitionsTransformation(repetitions_, delegate_->Clone());
  }

 private:
  size_t repetitions_;
  unique_ptr<Transformation> delegate_;
};

class ApplyRepetitionsTransformation : public Transformation {
 public:
  ApplyRepetitionsTransformation(int repetitions,
                                 unique_ptr<Transformation> delegate)
      : repetitions_(repetitions), delegate_(std::move(delegate)) {}

  void Apply(Result* result) const override {
    CHECK(result != nullptr);
    CHECK(result->buffer != nullptr);
    for (size_t i = 0; i < repetitions_; i++) {
      Result current_result(result->buffer);
      current_result.delete_buffer = result->delete_buffer;
      current_result.cursor = result->cursor;
      current_result.mode = result->mode;
      delegate_->Apply(&current_result);
      result->cursor = current_result.cursor;
      if (current_result.modified_buffer) {
        result->modified_buffer = true;
      }
      result->undo_stack->PushFront(std::move(current_result.undo_stack));
      if (!current_result.success) {
        result->success = false;
        LOG(INFO) << "Application " << i << " didn't succeed, giving up.";
        break;
      }
      if (current_result.made_progress) {
        result->made_progress = true;
      } else {
        LOG(INFO) << "Application " << i << " didn't make progress, giving up.";
        break;
      }
    }
  }

  unique_ptr<Transformation> Clone() const override {
    return NewApplyRepetitionsTransformation(repetitions_, delegate_->Clone());
  }

 private:
  size_t repetitions_;
  unique_ptr<Transformation> delegate_;
};

class DirectionTransformation : public Transformation {
 public:
  DirectionTransformation(Direction direction,
                          unique_ptr<Transformation> delegate)
      : direction_(direction), delegate_(std::move(delegate)) {}

  void Apply(Result* result) const override {
    CHECK(result != nullptr);
    CHECK(result->buffer != nullptr);
    auto editor_state = result->buffer->editor();
    auto original_direction = editor_state->direction();
    editor_state->set_direction(direction_);
    delegate_->Apply(result);
    editor_state->set_direction(original_direction);
  }

  unique_ptr<Transformation> Clone() const override {
    return NewDirectionTransformation(direction_, delegate_->Clone());
  }

 private:
  Direction direction_;
  unique_ptr<Transformation> delegate_;
};

class StructureTransformation : public Transformation {
 public:
  StructureTransformation(Structure* structure,
                          Modifiers::StructureRange structure_range,
                          unique_ptr<Transformation> delegate)
      : structure_(structure),
        structure_range_(structure_range),
        delegate_(std::move(delegate)) {}

  void Apply(Result* result) const override {
    CHECK(result != nullptr);
    CHECK(result->buffer != nullptr);
    auto editor_state = result->buffer->editor();
    auto original_structure = editor_state->structure();
    auto original_structure_range = editor_state->structure_range();
    editor_state->set_structure(structure_);
    editor_state->set_structure_range(structure_range_);
    delegate_->Apply(result);
    editor_state->set_structure(original_structure);
    editor_state->set_structure_range(original_structure_range);
  }

  unique_ptr<Transformation> Clone() const override {
    return NewStructureTransformation(structure_, structure_range_,
                                      delegate_->Clone());
  }

 private:
  Structure* const structure_;
  const Modifiers::StructureRange structure_range_;
  const std::unique_ptr<Transformation> delegate_;
};

}  // namespace

Transformation::Result::Result(OpenBuffer* buffer)
    : buffer(buffer),
      success(true),
      made_progress(false),
      modified_buffer(false),
      undo_stack(std::make_unique<TransformationStack>()),
      delete_buffer(std::make_shared<OpenBuffer>(buffer->editor(),
                                                 OpenBuffer::kPasteBuffer)) {}

unique_ptr<Transformation> NewInsertBufferTransformation(
    InsertOptions insert_options) {
  return std::make_unique<InsertBufferTransformation>(
      std::move(insert_options));
}

unique_ptr<Transformation> TransformationAtPosition(
    const LineColumn& position, unique_ptr<Transformation> transformation) {
  return ComposeTransformation(NewSetPositionTransformation(position),
                               std::move(transformation));
}

std::unique_ptr<Transformation> NewDeleteSuffixSuperfluousCharacters() {
  return std::make_unique<DeleteSuffixSuperfluousCharacters>();
}

std::unique_ptr<Transformation> NewSetRepetitionsTransformation(
    size_t repetitions, std::unique_ptr<Transformation> transformation) {
  return std::make_unique<SetRepetitionsTransformation>(
      repetitions, std::move(transformation));
}

std::unique_ptr<Transformation> NewApplyRepetitionsTransformation(
    size_t repetitions, unique_ptr<Transformation> transformation) {
  return std::make_unique<ApplyRepetitionsTransformation>(
      repetitions, std::move(transformation));
}

std::unique_ptr<Transformation> NewDirectionTransformation(
    Direction direction, std::unique_ptr<Transformation> transformation) {
  return std::make_unique<DirectionTransformation>(direction,
                                                   std::move(transformation));
}

std::unique_ptr<Transformation> NewStructureTransformation(
    Structure* structure, Modifiers::StructureRange structure_range,
    std::unique_ptr<Transformation> transformation) {
  return std::make_unique<StructureTransformation>(structure, structure_range,
                                                   std::move(transformation));
}

}  // namespace afc::editor
