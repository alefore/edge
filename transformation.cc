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

  unique_ptr<Transformation> Apply(EditorState*, OpenBuffer* buffer) const {
    LineColumn initial_position = buffer->position();
    buffer->set_position(position_);
    return NewGotoPositionTransformation(initial_position);
  }

  unique_ptr<Transformation> Clone() {
    return NewGotoPositionTransformation(position_);
  }

  virtual bool ModifiesBuffer() { return false; }

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

  unique_ptr<Transformation> Apply(
      EditorState* editor_state, OpenBuffer* buffer) const {
    LineColumn start_position = buffer->position();
    for (size_t i = 0; i < repetitions_; i++) {
      buffer->set_position(
          buffer->InsertInCurrentPosition(*buffer_to_insert_->contents()));
    }
    editor_state->ScheduleRedraw();
    if (final_position_ == START) {
      buffer->set_position(start_position);
    }

    return TransformationAtPosition(start_position,
        NewRepetitionsTransformation(
            buffer_to_insert_length_ * repetitions_,
            NewDeleteCharactersTransformation(false)));
  }

  unique_ptr<Transformation> Clone() {
    return NewInsertBufferTransformation(
        buffer_to_insert_, repetitions_, final_position_);
  }

  virtual bool ModifiesBuffer() { return true; }

 private:
  shared_ptr<OpenBuffer> buffer_to_insert_;
  size_t buffer_to_insert_length_;
  LineColumn position_;
  size_t repetitions_;
  InsertBufferTransformationPosition final_position_;
};

class NoopTransformation : public Transformation {
 public:
  unique_ptr<Transformation> Apply(EditorState*, OpenBuffer*) const {
    return NewNoopTransformation();
  }

  unique_ptr<Transformation> Clone() { return NewNoopTransformation(); }

  virtual bool ModifiesBuffer() { return false; }
};

class DeleteSuffixSuperfluousCharacters : public Transformation {
 public:
  unique_ptr<Transformation> Apply(
      EditorState* editor_state, OpenBuffer* buffer) const {
    const string& superfluous_characters(buffer->read_string_variable(
        OpenBuffer::variable_line_suffix_superfluous_characters()));
    const auto line = buffer->current_line();
    if (!line) { return NewNoopTransformation(); }
    size_t pos = line->size();
    while (pos > 0
           && superfluous_characters.find(line->get(pos - 1)) != string::npos) {
      pos--;
    }
    if (pos == line->size()) {
      return NewNoopTransformation();
    }
    CHECK_LT(pos, line->size());
    return TransformationAtPosition(
        LineColumn(buffer->position().line, pos),
        NewRepetitionsTransformation(line->size() - pos,
                                     NewDeleteCharactersTransformation(false)))
            ->Apply(editor_state, buffer);
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteSuffixSuperfluousCharacters();
  }

  virtual bool ModifiesBuffer() { return true; }
};

class RepetitionsTransformation : public Transformation {
 public:
  RepetitionsTransformation(int repetitions,
                            unique_ptr<Transformation> delegate)
      : repetitions_(repetitions),
        delegate_(std::move(delegate)) {}

  unique_ptr<Transformation> Apply(
      EditorState* editor_state, OpenBuffer* buffer) const {
    auto original_repetitions = editor_state->repetitions();
    editor_state->set_repetitions(repetitions_);
    auto result = delegate_->Apply(editor_state, buffer);
    editor_state->set_repetitions(original_repetitions);
    return std::move(result);
  }

  unique_ptr<Transformation> Clone() {
    return NewRepetitionsTransformation(repetitions_, delegate_->Clone());
  }

  virtual bool ModifiesBuffer() { return delegate_->ModifiesBuffer(); }

 private:
  size_t repetitions_;
  unique_ptr<Transformation> delegate_;
};

}  // namespace

namespace afc {
namespace editor {

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
