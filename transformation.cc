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
  for (auto it = buffer->line_begin(); it != buffer->line_end(); it++) {
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
    if (final_position_ == START) {
      buffer->set_position(start_position);
    }

    result->modified_buffer = true;
    result->undo = TransformationAtPosition(start_position,
        NewDeleteCharactersTransformation(
            buffer_to_insert_length_ * repetitions_,
            false));
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
    const string& superfluous_characters(buffer->read_string_variable(
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
    return TransformationAtPosition(
        LineColumn(buffer->position().line, pos),
        NewDeleteCharactersTransformation(line->size() - pos, false))
            ->Apply(editor_state, buffer, result);
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteSuffixSuperfluousCharacters();
  }
};

class RepetitionsTransformation : public Transformation {
 public:
  RepetitionsTransformation(int repetitions,
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
    return NewRepetitionsTransformation(repetitions_, delegate_->Clone());
  }

 private:
  size_t repetitions_;
  unique_ptr<Transformation> delegate_;
};

}  // namespace

namespace afc {
namespace editor {

Transformation::Result::Result()
     : success(true), modified_buffer(false), undo(NewNoopTransformation()) {}

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

unique_ptr<Transformation> NewRepetitionsTransformation(
    size_t repetitions, unique_ptr<Transformation> transformation) {
  return unique_ptr<Transformation>(
      new RepetitionsTransformation(repetitions, std::move(transformation)));
}


}  // namespace editor
}  // namespace afc
