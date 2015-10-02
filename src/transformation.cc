#include "transformation.h"

#include <glog/logging.h>

#include "buffer.h"
#include "editor.h"
#include "lazy_string_append.h"
#include "transformation_delete.h"

namespace {

using namespace afc::editor;

class GotoPositionTransformation : public Transformation {
 public:
  GotoPositionTransformation(const LineColumn& position)
      : position_(position) {}

  void Apply(EditorState*, OpenBuffer* buffer, Result* result) const {
    CHECK(buffer != nullptr);
    CHECK(result != nullptr);
    result->undo = NewGotoPositionTransformation(buffer->position());
    buffer->set_position(position_);
  }

  unique_ptr<Transformation> Clone() {
    return NewGotoPositionTransformation(position_);
  }

 private:
  LineColumn position_;
};

size_t CountCharacters(OpenBuffer* buffer) {
  size_t output = 0;
  for (auto it = buffer->begin(); it != buffer->end(); it++) {
    output += (*it)->size();
    output ++;
  }
  if (output > 0) {
    output--;  // Last line has no \n.
  }
  return output;
}

class InsertBufferTransformation : public Transformation {
 public:
  InsertBufferTransformation(
      shared_ptr<OpenBuffer> buffer_to_insert, size_t repetitions,
      InsertBufferTransformationPosition final_position)
      : buffer_to_insert_(buffer_to_insert),
        buffer_to_insert_length_(CountCharacters(buffer_to_insert.get())),
        repetitions_(repetitions),
        final_position_(final_position) {
    CHECK(buffer_to_insert_ != nullptr);
  }

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    LineColumn start_position = buffer->position();
    for (size_t i = 0; i < repetitions_; i++) {
      buffer->set_position(
          buffer->InsertInCurrentPosition(*buffer_to_insert_->contents()));
    }
    editor_state->ScheduleRedraw();

    size_t chars_inserted = buffer_to_insert_length_ * repetitions_;
    unique_ptr<TransformationStack> undo_stack(new TransformationStack());
    Modifiers modifiers;
    modifiers.repetitions = chars_inserted;
    undo_stack->PushFront(
        TransformationAtPosition(start_position,
            NewDeleteCharactersTransformation(modifiers, false)));

    if (editor_state->insertion_modifier() == Modifiers::REPLACE) {
      Result current_result(editor_state);
      Modifiers modifiers;
      modifiers.repetitions = chars_inserted;
      NewDeleteCharactersTransformation(modifiers, false)
          ->Apply(editor_state, buffer, &current_result);
      undo_stack->PushFront(std::move(current_result.undo));
    }

    if (final_position_ == START) {
      buffer->set_position(start_position);
    }

    result->modified_buffer = true;
    result->made_progress = true;
    result->undo = std::move(undo_stack);
  }

  unique_ptr<Transformation> Clone() {
    return NewInsertBufferTransformation(
        buffer_to_insert_, repetitions_, final_position_);
  }

 private:
  shared_ptr<OpenBuffer> buffer_to_insert_;
  size_t buffer_to_insert_length_;
  LineColumn position_;
  size_t repetitions_;
  InsertBufferTransformationPosition final_position_;
};

class NoopTransformation : public Transformation {
 public:
  void Apply(EditorState*, OpenBuffer*, Result*) const {}

  unique_ptr<Transformation> Clone() { return NewNoopTransformation(); }
};

class DeleteSuffixSuperfluousCharacters : public Transformation {
 public:
  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    const wstring& superfluous_characters(buffer->read_string_variable(
        OpenBuffer::variable_line_suffix_superfluous_characters()));
    const auto line = buffer->current_line();
    if (!line) { return; }
    size_t pos = line->size();
    while (pos > 0
           && superfluous_characters.find(line->get(pos - 1)) != string::npos) {
      pos--;
    }
    if (pos == line->size()) {
      return;
    }
    CHECK_LT(pos, line->size());
    Modifiers modifiers;
    modifiers.repetitions = line->size() - pos;
    return TransformationAtPosition(
        LineColumn(buffer->position().line, pos),
        NewDeleteCharactersTransformation(modifiers, false))
            ->Apply(editor_state, buffer, result);
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteSuffixSuperfluousCharacters();
  }
};

class SetRepetitionsTransformation : public Transformation {
 public:
  SetRepetitionsTransformation(int repetitions,
                               unique_ptr<Transformation> delegate)
      : repetitions_(repetitions),
        delegate_(std::move(delegate)) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    auto original_repetitions = editor_state->repetitions();
    editor_state->set_repetitions(repetitions_);
    delegate_->Apply(editor_state, buffer, result);
    editor_state->set_repetitions(original_repetitions);
  }

  unique_ptr<Transformation> Clone() {
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
      : repetitions_(repetitions),
        delegate_(std::move(delegate)) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    unique_ptr<TransformationStack> undo_stack(new TransformationStack);
    for (size_t i = 0; i < repetitions_; i++) {
      Result current_result(editor_state);
      current_result.delete_buffer = result->delete_buffer;
      delegate_->Apply(editor_state, buffer, &current_result);
      if (current_result.modified_buffer) {
        result->modified_buffer = true;
      }
      undo_stack->PushFront(std::move(current_result.undo));
      if (!current_result.success) {
        LOG(INFO) << "Application " << i << " didn't succeed, giving up.";
        break;
      }
      if (!current_result.made_progress) {
        LOG(INFO) << "Application " << i << " didn't make progress, giving up.";
        break;
      }
    }
    result->undo = std::move(undo_stack);
  }

  unique_ptr<Transformation> Clone() {
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
      : direction_(direction),
        delegate_(std::move(delegate)) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    auto original_direction = editor_state->direction();
    editor_state->set_direction(direction_);
    delegate_->Apply(editor_state, buffer, result);
    editor_state->set_direction(original_direction);
  }

  unique_ptr<Transformation> Clone() {
    return NewDirectionTransformation(direction_, delegate_->Clone());
  }

 private:
  Direction direction_;
  unique_ptr<Transformation> delegate_;
};

class StructureTransformation : public Transformation {
 public:
  StructureTransformation(Structure structure,
                          Modifiers::StructureRange structure_range,
                          unique_ptr<Transformation> delegate)
      : structure_(structure),
        structure_range_(structure_range),
        delegate_(std::move(delegate)) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    auto original_structure = editor_state->structure();
    auto original_structure_range = editor_state->structure_range();
    editor_state->set_structure(structure_);
    editor_state->set_structure_range(structure_range_);
    delegate_->Apply(editor_state, buffer, result);
    editor_state->set_structure(original_structure);
    editor_state->set_structure_range(original_structure_range);
  }

  unique_ptr<Transformation> Clone() {
    return NewStructureTransformation(
        structure_, structure_range_, delegate_->Clone());
  }

 private:
  Structure structure_;
  Modifiers::StructureRange structure_range_;
  unique_ptr<Transformation> delegate_;
};

}  // namespace

namespace afc {
namespace editor {

Transformation::Result::Result(EditorState* editor_state)
     : success(true),
       made_progress(false),
       modified_buffer(false),
       undo(NewNoopTransformation()),
       delete_buffer(new OpenBuffer(editor_state, OpenBuffer::kPasteBuffer)) {}

unique_ptr<Transformation> NewInsertBufferTransformation(
    shared_ptr<OpenBuffer> buffer_to_insert, size_t repetitions,
    InsertBufferTransformationPosition final_position) {
  return unique_ptr<Transformation>(new InsertBufferTransformation(
      buffer_to_insert, repetitions, final_position));
}

unique_ptr<Transformation> NewGotoPositionTransformation(
    const LineColumn& position) {
  return unique_ptr<Transformation>(new GotoPositionTransformation(position));
}

unique_ptr<Transformation> NewNoopTransformation() {
  return unique_ptr<Transformation>(new NoopTransformation());
}

unique_ptr<Transformation> ComposeTransformation(
    unique_ptr<Transformation> a, unique_ptr<Transformation> b) {
  unique_ptr<TransformationStack> stack(new TransformationStack());
  stack->PushBack(std::move(a));
  stack->PushBack(std::move(b));
  return std::move(stack);
}

unique_ptr<Transformation> TransformationAtPosition(
    const LineColumn& position, unique_ptr<Transformation> transformation) {
  return ComposeTransformation(
      NewGotoPositionTransformation(position), std::move(transformation));
}

unique_ptr<Transformation> NewDeleteSuffixSuperfluousCharacters() {
  return unique_ptr<Transformation>(new DeleteSuffixSuperfluousCharacters());
}

unique_ptr<Transformation> NewSetRepetitionsTransformation(
    size_t repetitions, unique_ptr<Transformation> transformation) {
  return unique_ptr<Transformation>(
      new SetRepetitionsTransformation(repetitions, std::move(transformation)));
}

unique_ptr<Transformation> NewApplyRepetitionsTransformation(
    size_t repetitions, unique_ptr<Transformation> transformation) {
  return unique_ptr<Transformation>(new ApplyRepetitionsTransformation(
      repetitions, std::move(transformation)));
}

unique_ptr<Transformation> NewDirectionTransformation(
    Direction direction, unique_ptr<Transformation> transformation) {
  return unique_ptr<Transformation>(
      new DirectionTransformation(direction, std::move(transformation)));
}

unique_ptr<Transformation> NewStructureTransformation(
    Structure structure,
    Modifiers::StructureRange structure_range,
    unique_ptr<Transformation> transformation) {
  return unique_ptr<Transformation>(new StructureTransformation(
      structure, structure_range, std::move(transformation)));
}

}  // namespace editor
}  // namespace afc
