#include "transformation.h"

#include "buffer.h"
#include "editor.h"
#include "lazy_string_append.h"

namespace {

using namespace afc::editor;

class InsertBuffer : public Transformation {
 public:
  InsertBuffer(shared_ptr<OpenBuffer> buffer_to_insert,
               const LineColumn& position, size_t repetitions)
      : buffer_to_insert_(buffer_to_insert),
        position_(position),
        repetitions_(repetitions) {
    assert(buffer_to_insert_ != nullptr);
  }

  unique_ptr<Transformation> Apply(
      EditorState* editor_state, OpenBuffer* buffer) const {
    LineColumn position = position_;
    for (size_t i = 0; i < repetitions_; i++) {
      position = buffer->InsertInPosition(
          *buffer_to_insert_->contents(), position);
    }
    buffer->set_position(position);
    editor_state->ScheduleRedraw();
    return NewDeleteTransformation(position_, position);
  }

 private:
  shared_ptr<OpenBuffer> buffer_to_insert_;
  LineColumn position_;
  size_t repetitions_;
};

void InsertDeletedTextBuffer(EditorState* editor_state,
                             const shared_ptr<OpenBuffer>& buffer) {
  auto insert_result = editor_state->buffers()->insert(
      make_pair(OpenBuffer::kPasteBuffer, buffer));
  if (!insert_result.second) {
    insert_result.first->second = buffer;
  }
}

// A transformation that, when applied, removes the text from the start position
// to the end position (leaving the characters immediately at the end position).
class DeleteTransformation : public Transformation {
 public:
  DeleteTransformation(const LineColumn& start, const LineColumn& end)
      : start_(start), end_(end) {}
      
  unique_ptr<Transformation> Apply(
      EditorState* editor_state, OpenBuffer* buffer) const {
    shared_ptr<OpenBuffer> deleted_text(
        new OpenBuffer(editor_state, OpenBuffer::kPasteBuffer));
    size_t actual_end = min(end_.line, buffer->contents()->size() - 1);
    for (size_t line = start_.line; line <= actual_end; line++) {
      auto current_line = buffer->contents()->at(line);
      size_t current_start_column = line == start_.line ? start_.column : 0;
      size_t current_end_column =
          line == end_.line ? end_.column : current_line->size();
      deleted_text->AppendLine(
          editor_state,
          Substring(current_line->contents, current_start_column,
                    current_end_column - current_start_column));
      if (current_line->activate != nullptr
          && current_end_column == current_line->size()) {
        current_line->activate->ProcessInput('d', editor_state);
      }
    }
    deleted_text->contents()->erase(deleted_text->contents()->end() - 1);
    shared_ptr<LazyString> prefix = Substring(
        buffer->contents()->at(start_.line)->contents, 0, start_.column);
    shared_ptr<LazyString> contents_last_line =
        end_.line < buffer->contents()->size()
        ? StringAppend(prefix,
                       Substring(buffer->contents()->at(end_.line)->contents,
                                 end_.column))
        : prefix;
    buffer->contents()->erase(
        buffer->contents()->begin() + start_.line + 1,
        buffer->contents()->begin() + actual_end + 1);
    buffer->contents()->at(start_.line).reset(new Line(contents_last_line));
    buffer->contents()->at(start_.line)->modified = true;
    buffer->set_position(start_);
    buffer->CheckPosition();
    assert(deleted_text != nullptr);

    editor_state->ScheduleRedraw();

    InsertDeletedTextBuffer(editor_state, deleted_text);

    return NewInsertBufferTransformation(deleted_text, start_, 1);
  }

 private:
  LineColumn start_;
  LineColumn end_;
};

}  // namespace

namespace afc {
namespace editor {

unique_ptr<Transformation> NewInsertBufferTransformation(
    shared_ptr<OpenBuffer> buffer_to_insert,
    const LineColumn& position, size_t repetitions) {
  return unique_ptr<Transformation>(
      new InsertBuffer(buffer_to_insert, position, repetitions));
}

unique_ptr<Transformation> NewDeleteTransformation(
    const LineColumn& start, const LineColumn& end) {
  return unique_ptr<Transformation>(new DeleteTransformation(start, end));
}

}  // namespace editor
}  // namespace afc
