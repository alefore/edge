#include "src/transformation.h"

#include <glog/logging.h>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/lazy_string_append.h"
#include "src/transformation_delete.h"

namespace {

using namespace afc::editor;

class GotoColumnTransformation : public Transformation {
 public:
  GotoColumnTransformation(size_t column) : column_(column) {}

  void Apply(OpenBuffer* buffer, Result* result) const override {
    CHECK(buffer != nullptr);
    CHECK(result != nullptr);
    auto line = buffer->LineAt(result->cursor.line);
    if (line == nullptr) {
      return;
    }

    result->undo_stack->PushFront(
        NewGotoColumnTransformation(result->cursor.column));
    result->cursor.column = std::min(column_, line->size());
    result->success = true;
  }

  std::unique_ptr<Transformation> Clone() const override {
    return NewGotoColumnTransformation(column_);
  }

 private:
  const size_t column_;
};

class GotoPositionTransformation : public Transformation {
 public:
  GotoPositionTransformation(const LineColumn& position)
      : position_(position) {}

  void Apply(OpenBuffer* buffer, Result* result) const override {
    CHECK(buffer != nullptr);
    CHECK(result != nullptr);
    result->undo_stack->PushFront(
        NewGotoPositionTransformation(result->cursor));
    result->cursor = position_;
    result->success = true;
  }

  std::unique_ptr<Transformation> Clone() const override {
    return NewGotoPositionTransformation(position_);
  }

 private:
  const LineColumn position_;
};

class InsertBufferTransformation : public Transformation {
 public:
  InsertBufferTransformation(InsertOptions options)
      : options_(std::move(options)),
        buffer_to_insert_length_(
            options_.buffer_to_insert->contents()->CountCharacters()) {
    CHECK(options_.buffer_to_insert != nullptr);
  }

  void Apply(OpenBuffer* buffer, Result* result) const override {
    LineColumn position = options_.position.has_value()
                              ? options_.position.value()
                              : result->cursor;
    buffer->AdjustLineColumn(&position);
    LineColumn start_position = position;
    for (size_t i = 0; i < options_.modifiers.repetitions; i++) {
      position = buffer->InsertInPosition(*options_.buffer_to_insert, position,
                                          options_.modifiers_set);
    }

    if (!options_.position.has_value()) {
      result->cursor = position;
    }

    buffer->editor()->ScheduleRedraw();

    size_t chars_inserted =
        buffer_to_insert_length_ * options_.modifiers.repetitions;
    DeleteOptions delete_options;
    delete_options.modifiers.repetitions = chars_inserted;
    delete_options.copy_to_paste_buffer = false;
    result->undo_stack->PushFront(TransformationAtPosition(
        start_position, NewDeleteTransformation(delete_options)));

    if (options_.modifiers.insertion == Modifiers::REPLACE) {
      Result current_result(buffer->editor());
      DeleteOptions delete_options;
      delete_options.modifiers.repetitions = chars_inserted;
      delete_options.copy_to_paste_buffer = false;
      delete_options.line_end_behavior = DeleteOptions::LineEndBehavior::kStop;
      TransformationAtPosition(position,
                               NewDeleteTransformation(delete_options))
          ->Apply(buffer, &current_result);
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

class NoopTransformation : public Transformation {
 public:
  void Apply(OpenBuffer*, Result*) const override {}

  unique_ptr<Transformation> Clone() const override {
    return NewNoopTransformation();
  }
};

class DeleteSuffixSuperfluousCharacters : public Transformation {
 public:
  void Apply(OpenBuffer* buffer, Result* result) const override {
    const wstring& superfluous_characters(
        buffer->Read(buffer_variables::line_suffix_superfluous_characters()));
    const auto line = buffer->LineAt(result->cursor.line);
    if (line == nullptr) {
      result->made_progress = false;
      return;
    }
    size_t pos = line->size();
    while (pos > 0 &&
           superfluous_characters.find(line->get(pos - 1)) != string::npos) {
      pos--;
    }
    if (pos == line->size()) {
      return;
    }
    CHECK_LT(pos, line->size());
    DeleteOptions delete_options;
    delete_options.modifiers.repetitions = line->size() - pos;
    delete_options.copy_to_paste_buffer = false;
    return TransformationAtPosition(LineColumn(result->cursor.line, pos),
                                    NewDeleteTransformation(delete_options))
        ->Apply(buffer, result);
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

  void Apply(OpenBuffer* buffer, Result* result) const override {
    auto editor_state = buffer->editor();
    auto original_repetitions = editor_state->repetitions();
    editor_state->set_repetitions(repetitions_);
    delegate_->Apply(buffer, result);
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

  void Apply(OpenBuffer* buffer, Result* result) const override {
    for (size_t i = 0; i < repetitions_; i++) {
      Result current_result(buffer->editor());
      current_result.delete_buffer = result->delete_buffer;
      current_result.cursor = result->cursor;
      current_result.mode = result->mode;
      delegate_->Apply(buffer, &current_result);
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

  void Apply(OpenBuffer* buffer, Result* result) const override {
    auto editor_state = buffer->editor();
    auto original_direction = editor_state->direction();
    editor_state->set_direction(direction_);
    delegate_->Apply(buffer, result);
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

  void Apply(OpenBuffer* buffer, Result* result) const override {
    auto editor_state = buffer->editor();
    auto original_structure = editor_state->structure();
    auto original_structure_range = editor_state->structure_range();
    editor_state->set_structure(structure_);
    editor_state->set_structure_range(structure_range_);
    delegate_->Apply(buffer, result);
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

namespace afc {
namespace editor {

Transformation::Result::Result(EditorState* editor_state)
    : success(true),
      made_progress(false),
      modified_buffer(false),
      undo_stack(std::make_unique<TransformationStack>()),
      delete_buffer(std::make_shared<OpenBuffer>(editor_state,
                                                 OpenBuffer::kPasteBuffer)) {}

unique_ptr<Transformation> NewInsertBufferTransformation(
    InsertOptions insert_options) {
  return std::make_unique<InsertBufferTransformation>(
      std::move(insert_options));
}

std::unique_ptr<Transformation> NewGotoColumnTransformation(size_t column) {
  return std::make_unique<GotoColumnTransformation>(column);
}

std::unique_ptr<Transformation> NewGotoPositionTransformation(
    const LineColumn& position) {
  return std::make_unique<GotoPositionTransformation>(position);
}

std::unique_ptr<Transformation> NewNoopTransformation() {
  return std::make_unique<NoopTransformation>();
}

std::unique_ptr<Transformation> ComposeTransformation(
    std::unique_ptr<Transformation> a, std::unique_ptr<Transformation> b) {
  auto stack = std::make_unique<TransformationStack>();
  stack->PushBack(std::move(a));
  stack->PushBack(std::move(b));
  return std::move(stack);
}

unique_ptr<Transformation> TransformationAtPosition(
    const LineColumn& position, unique_ptr<Transformation> transformation) {
  return ComposeTransformation(NewGotoPositionTransformation(position),
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

void TransformationStack::Apply(OpenBuffer* buffer, Result* result) const {
  CHECK(result != nullptr);
  for (auto& it : stack_) {
    Result it_result(buffer->editor());
    it_result.mode = result->mode;
    it_result.delete_buffer = result->delete_buffer;
    it_result.cursor = result->cursor;
    it->Apply(buffer, &it_result);
    result->cursor = it_result.cursor;
    if (it_result.modified_buffer) {
      result->modified_buffer = true;
    }
    if (it_result.made_progress) {
      result->made_progress = true;
    }
    result->undo_stack->PushFront(std::move(it_result.undo_stack));
    if (!it_result.success) {
      result->success = false;
      break;
    }
  }
}

}  // namespace editor
}  // namespace afc
