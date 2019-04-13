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

  void Apply(EditorState*, OpenBuffer* buffer, Result* result) const override {
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

  void Apply(EditorState*, OpenBuffer* buffer, Result* result) const override {
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
  InsertBufferTransformation(shared_ptr<const OpenBuffer> buffer_to_insert,
                             Modifiers modifiers,
                             InsertBufferTransformationPosition final_position,
                             LineModifierSet* modifiers_set)
      : buffer_to_insert_(buffer_to_insert),
        buffer_to_insert_length_(
            buffer_to_insert->contents()->CountCharacters()),
        modifiers_(modifiers),
        final_position_(final_position),
        modifiers_set_(modifiers_set) {
    CHECK(buffer_to_insert_ != nullptr);
  }

  void Apply(EditorState* editor_state, OpenBuffer* buffer,
             Result* result) const override {
    LineColumn start_position = result->cursor;
    buffer->AdjustLineColumn(&start_position);
    for (size_t i = 0; i < modifiers_.repetitions; i++) {
      result->cursor = buffer->InsertInPosition(*buffer_to_insert_,
                                                result->cursor, modifiers_set_);
    }
    editor_state->ScheduleRedraw();

    size_t chars_inserted = buffer_to_insert_length_ * modifiers_.repetitions;
    DeleteOptions delete_options;
    delete_options.modifiers.repetitions = chars_inserted;
    delete_options.copy_to_paste_buffer = false;
    result->undo_stack->PushFront(TransformationAtPosition(
        start_position, NewDeleteTransformation(delete_options)));

    if (modifiers_.insertion == Modifiers::REPLACE) {
      Result current_result(editor_state);
      DeleteOptions delete_options;
      delete_options.modifiers.repetitions = chars_inserted;
      delete_options.copy_to_paste_buffer = false;
      delete_options.line_end_behavior = DeleteOptions::LineEndBehavior::kStop;
      TransformationAtPosition(result->cursor,
                               NewDeleteTransformation(delete_options))
          ->Apply(editor_state, buffer, &current_result);
      result->undo_stack->PushFront(std::move(current_result.undo_stack));
    }

    if (final_position_ == START) {
      result->cursor = start_position;
    }

    result->modified_buffer = true;
    result->made_progress = true;
  }

  unique_ptr<Transformation> Clone() const override {
    return NewInsertBufferTransformation(buffer_to_insert_, modifiers_,
                                         final_position_, modifiers_set_);
  }

 private:
  shared_ptr<const OpenBuffer> buffer_to_insert_;
  size_t buffer_to_insert_length_;
  Modifiers modifiers_;
  InsertBufferTransformationPosition final_position_;
  LineModifierSet* modifiers_set_;
};

class NoopTransformation : public Transformation {
 public:
  void Apply(EditorState*, OpenBuffer*, Result*) const override {}

  unique_ptr<Transformation> Clone() const override {
    return NewNoopTransformation();
  }
};

class DeleteSuffixSuperfluousCharacters : public Transformation {
 public:
  void Apply(EditorState* editor_state, OpenBuffer* buffer,
             Result* result) const override {
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
        ->Apply(editor_state, buffer, result);
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

  void Apply(EditorState* editor_state, OpenBuffer* buffer,
             Result* result) const override {
    auto original_repetitions = editor_state->repetitions();
    editor_state->set_repetitions(repetitions_);
    delegate_->Apply(editor_state, buffer, result);
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

  void Apply(EditorState* editor_state, OpenBuffer* buffer,
             Result* result) const override {
    for (size_t i = 0; i < repetitions_; i++) {
      Result current_result(editor_state);
      current_result.delete_buffer = result->delete_buffer;
      current_result.cursor = result->cursor;
      current_result.mode = result->mode;
      delegate_->Apply(editor_state, buffer, &current_result);
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

  void Apply(EditorState* editor_state, OpenBuffer* buffer,
             Result* result) const override {
    auto original_direction = editor_state->direction();
    editor_state->set_direction(direction_);
    delegate_->Apply(editor_state, buffer, result);
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

  void Apply(EditorState* editor_state, OpenBuffer* buffer,
             Result* result) const override {
    auto original_structure = editor_state->structure();
    auto original_structure_range = editor_state->structure_range();
    editor_state->set_structure(structure_);
    editor_state->set_structure_range(structure_range_);
    delegate_->Apply(editor_state, buffer, result);
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
    shared_ptr<const OpenBuffer> buffer_to_insert, Modifiers modifiers,
    InsertBufferTransformationPosition final_position,
    LineModifierSet* modifiers_set) {
  return std::make_unique<InsertBufferTransformation>(
      buffer_to_insert, modifiers, final_position, modifiers_set);
}

unique_ptr<Transformation> NewInsertBufferTransformation(
    shared_ptr<const OpenBuffer> buffer_to_insert, size_t repetitions,
    InsertBufferTransformationPosition final_position) {
  Modifiers modifiers;
  modifiers.repetitions = repetitions;
  return NewInsertBufferTransformation(buffer_to_insert, modifiers,
                                       final_position, nullptr);
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

}  // namespace editor
}  // namespace afc
