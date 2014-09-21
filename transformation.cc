#include "transformation.h"

#include <glog/logging.h>

#include "buffer.h"
#include "editor.h"
#include "lazy_string_append.h"

namespace {

using namespace afc::editor;

class GotoPositionTransformation : public Transformation {
 public:
  GotoPositionTransformation(const LineColumn& position)
      : position_(position) {}

  unique_ptr<Transformation> Apply(EditorState*, OpenBuffer* buffer) const {
    LineColumn initial_position = buffer->position();
    buffer->set_position(position_);
    return unique_ptr<Transformation>(
        new GotoPositionTransformation(initial_position));
  }

 private:
  LineColumn position_;
};

class InsertBufferTransformation : public Transformation {
 public:
  InsertBufferTransformation(
      shared_ptr<OpenBuffer> buffer_to_insert, size_t repetitions,
      InsertBufferTransformationPosition final_position)
      : buffer_to_insert_(buffer_to_insert),
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
    return NewDeleteTransformation(start_position, buffer->position(), false);
  }

 private:
  shared_ptr<OpenBuffer> buffer_to_insert_;
  LineColumn position_;
  size_t repetitions_;
  InsertBufferTransformationPosition final_position_;
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
  DeleteTransformation(const LineColumn& start, const LineColumn& end,
                       bool copy_to_paste_buffer)
      : start_(start), end_(end), copy_to_paste_buffer_(copy_to_paste_buffer) {}

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
      assert(current_start_column <= current_end_column);
      deleted_text->AppendLine(
          editor_state,
          current_line->Substring(
              current_start_column,
              current_end_column - current_start_column));
      if (current_line->activate() != nullptr
          && current_end_column == current_line->size()) {
        current_line->activate()->ProcessInput('d', editor_state);
      }
    }
    shared_ptr<LazyString> prefix =
        buffer->contents()->at(start_.line)->Substring(0, start_.column);
    shared_ptr<LazyString> contents_last_line =
        end_.line < buffer->contents()->size()
        ? StringAppend(prefix,
              buffer->contents()->at(end_.line)->Substring(end_.column))
        : prefix;
    buffer->contents()->erase(
        buffer->contents()->begin() + start_.line,
        buffer->contents()->begin() + actual_end);
    if (buffer->contents()->at(start_.line)->contents() != contents_last_line) {
      buffer->contents()->at(start_.line).reset(
          new Line(Line::Options(contents_last_line)));
      buffer->contents()->at(start_.line)->set_modified(true);
    }
    buffer->set_position(start_);
    buffer->CheckPosition();

    editor_state->ScheduleRedraw();

    if (copy_to_paste_buffer_) {
      InsertDeletedTextBuffer(editor_state, deleted_text);
    }

    return NewInsertBufferTransformation(deleted_text, 1, START);
  }

 private:
  LineColumn start_;
  LineColumn end_;
  bool copy_to_paste_buffer_;
};

class DeleteCharactersTransformation : public Transformation {
 public:
  DeleteCharactersTransformation(size_t repetitions, bool copy_to_paste_buffer)
      : repetitions_(repetitions),
        copy_to_paste_buffer_(copy_to_paste_buffer) {}

  unique_ptr<Transformation> Apply(
      EditorState* editor_state, OpenBuffer* buffer) const {
    buffer->CheckPosition();
    buffer->MaybeAdjustPositionCol();
    shared_ptr<OpenBuffer> deleted_text(
        new OpenBuffer(editor_state, OpenBuffer::kPasteBuffer));
    bool needs_empty_line = false;
    size_t characters_erased = 0;
    while (characters_erased < repetitions_) {
      LOG(INFO) << "Erased " << characters_erased << " of " << repetitions_;
      if (buffer->line() == buffer->line_end()) {
        LOG(INFO) << "Giving up: at end of file.";
        characters_erased = repetitions_;
        continue;
      }
      size_t characters_left =
          (*buffer->line())->size() - buffer->current_position_col();
      if (repetitions_ - characters_erased <= characters_left
          || buffer->line().line() + 1 == buffer->line_end().line()) {
        size_t characters_to_erase = min(
            (*buffer->line())->size() - buffer->current_position_col(),
            repetitions_ - characters_erased);
        LOG(INFO) << "Not enough characters left ("
                  << characters_to_erase << ")";
        characters_erased = repetitions_;
        deleted_text->AppendLine(editor_state,
            (*buffer->line())->Substring(
                buffer->current_position_col(), characters_to_erase));
        buffer->contents()->at(buffer->line().line()).reset(
            new Line(Line::Options(StringAppend(
                (*buffer->line())->Substring(0, buffer->current_position_col()),
                (*buffer->line())->Substring(
                    buffer->current_position_col() + characters_to_erase)))));
        needs_empty_line = false;
        continue;
      }
      characters_erased += characters_left + 1;
      LOG(INFO) << "Erasing characters " << characters_left + 1 << " from line "
                << buffer->line().line();
      deleted_text->AppendLine(editor_state,
          (*buffer->line())->Substring(buffer->current_position_col()));
      LOG(INFO) << "Overwriting line: " << buffer->line().line();
      buffer->contents()->at(buffer->line().line()).reset(
          new Line(Line::Options(StringAppend(
              (*buffer->line())->Substring(0, buffer->current_position_col()),
              buffer->contents()->at(buffer->line().line() + 1)->contents()))));
      buffer->contents()->erase(
          buffer->contents()->begin() + buffer->line().line() + 1);
      needs_empty_line = true;
    }
    if (copy_to_paste_buffer_) {
      if (needs_empty_line) {
        deleted_text->AppendLine(editor_state, EmptyString());
      }
      InsertDeletedTextBuffer(editor_state, deleted_text);
    }
    return TransformationAtPosition(buffer->position(),
        NewInsertBufferTransformation(deleted_text, 1, START));
  }

 private:
  size_t repetitions_;
  bool copy_to_paste_buffer_;
};

class DeleteWordsTransformation : public Transformation {
 public:
  DeleteWordsTransformation(size_t repetitions, bool copy_to_paste_buffer)
      : repetitions_(repetitions),
        copy_to_paste_buffer_(copy_to_paste_buffer) {}

  unique_ptr<Transformation> Apply(
      EditorState* editor_state, OpenBuffer* buffer) const {
    shared_ptr<OpenBuffer> deleted_text(
        new OpenBuffer(editor_state, OpenBuffer::kPasteBuffer));
    unique_ptr<TransformationStack> stack(new TransformationStack);
    for (size_t i = 0; i < repetitions_; i++) {
      LineColumn initial_position = buffer->position();
      LineColumn start, end;
      if (!buffer->BoundWordAt(initial_position, &start, &end)) {
        LOG(INFO) << "Unable to bound word, giving up.";
        return NewNoopTransformation();
      }
      CHECK_EQ(start.line, end.line);
      CHECK_LE(start.column + 1, end.column);
      size_t characters_to_erase;
      if (start.column < initial_position.column) {
        buffer->set_position(start);
        characters_to_erase = end.column - start.column;
      } else if (initial_position.line == end.line) {
        characters_to_erase = end.column - initial_position.column;
      } else {
        characters_to_erase = 0;
      }
      LOG(INFO) << "Erasing word, characters: " << characters_to_erase;
      stack->PushFront(unique_ptr<Transformation>(
          new GotoPositionTransformation(initial_position)));
      stack->PushFront(
          NewDeleteCharactersTransformation(characters_to_erase, true)
              ->Apply(editor_state, buffer));
    }
    return std::move(stack);
  }

 private:
  size_t repetitions_;
  bool copy_to_paste_buffer_;
};

class DeleteLinesTransformation : public Transformation {
 public:
  DeleteLinesTransformation(size_t repetitions, bool copy_to_paste_buffer)
      : repetitions_(repetitions),
        copy_to_paste_buffer_(copy_to_paste_buffer) {}

  unique_ptr<Transformation> Apply(
      EditorState* editor_state, OpenBuffer* buffer) const {
    BufferLineIterator line = buffer->line();
    size_t repetitions = min(repetitions_,
        buffer->contents()->size() - line.line());
    shared_ptr<OpenBuffer> deleted_text(
        new OpenBuffer(editor_state, OpenBuffer::kPasteBuffer));
    LOG(INFO) << "Erasing lines " << repetitions << " starting at line "
         << buffer->line().line() << " in a buffer with size "
         << buffer->contents()->size();
    for (size_t i = 0; i < repetitions; i++) {
      deleted_text->AppendLine(editor_state, (*line)->contents());
      if ((*line)->activate() != nullptr) {
        (*line)->activate()->ProcessInput('d', editor_state);
      }
      line++;
    }
    deleted_text->AppendLine(editor_state, EmptyString());
    buffer->contents()->erase(
        buffer->contents()->begin() + buffer->line().line(),
        buffer->contents()->begin() + buffer->line().line() + repetitions);
    if (copy_to_paste_buffer_) {
      InsertDeletedTextBuffer(editor_state, deleted_text);
    }
    return TransformationAtPosition(LineColumn(buffer->position().line),
        ComposeTransformation(
            NewInsertBufferTransformation(deleted_text, 1, END),
            unique_ptr<Transformation>(
                new GotoPositionTransformation(buffer->position()))));
  }

 private:
  size_t repetitions_;
  bool copy_to_paste_buffer_;
};

class NoopTransformation : public Transformation {
  unique_ptr<Transformation> Apply(EditorState*, OpenBuffer*) const {
    return NewNoopTransformation();
  }
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

unique_ptr<Transformation> NewDeleteCharactersTransformation(
    size_t repetitions, bool copy_to_paste_buffer) {
  return unique_ptr<Transformation>(
      new DeleteCharactersTransformation(repetitions, copy_to_paste_buffer));
}

unique_ptr<Transformation> NewDeleteWordsTransformation(
    size_t repetitions, bool copy_to_paste_buffer) {
  return unique_ptr<Transformation>(
      new DeleteWordsTransformation(repetitions, copy_to_paste_buffer));
}

unique_ptr<Transformation> NewDeleteLinesTransformation(
    size_t repetitions, bool copy_to_paste_buffer) {
  return unique_ptr<Transformation>(
      new DeleteLinesTransformation(repetitions, copy_to_paste_buffer));
}

unique_ptr<Transformation> NewDeleteTransformation(
    const LineColumn& start, const LineColumn& end, bool copy_to_paste_buffer) {
  return unique_ptr<Transformation>(
      new DeleteTransformation(start, end, copy_to_paste_buffer));
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
      unique_ptr<Transformation>(new GotoPositionTransformation(position)),
      std::move(transformation));
}

}  // namespace editor
}  // namespace afc