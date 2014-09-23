#include "transformation_delete.h"

#include <glog/logging.h>

#include "buffer.h"
#include "direction.h"
#include "editor.h"
#include "lazy_string_append.h"
#include "transformation.h"

namespace afc {
namespace editor {

void InsertDeletedTextBuffer(EditorState* editor_state,
                             const shared_ptr<OpenBuffer>& buffer) {
  auto insert_result = editor_state->buffers()->insert(
      make_pair(OpenBuffer::kPasteBuffer, buffer));
  if (!insert_result.second) {
    insert_result.first->second = buffer;
  }
}

namespace {
class DeleteCharactersTransformation : public Transformation {
 public:
  DeleteCharactersTransformation(size_t repetitions, bool copy_to_paste_buffer)
      : repetitions_(repetitions),
        copy_to_paste_buffer_(copy_to_paste_buffer) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
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
        result->success = false;
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
        result->modified_buffer = true;
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
      result->modified_buffer = true;
      needs_empty_line = true;
    }
    if (copy_to_paste_buffer_) {
      if (needs_empty_line) {
        deleted_text->AppendLine(editor_state, EmptyString());
      }
      InsertDeletedTextBuffer(editor_state, deleted_text);
    }
    result->undo = TransformationAtPosition(buffer->position(),
        NewInsertBufferTransformation(deleted_text, 1, START));
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteCharactersTransformation(
        repetitions_, copy_to_paste_buffer_);
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

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    CHECK(result != nullptr);
    shared_ptr<OpenBuffer> deleted_text(
        new OpenBuffer(editor_state, OpenBuffer::kPasteBuffer));
    unique_ptr<TransformationStack> stack(new TransformationStack);
    for (size_t i = 0; i < repetitions_; i++) {
      LineColumn initial_position = buffer->position();
      LineColumn start, end;
      if (!buffer->BoundWordAt(initial_position, &start, &end)) {
        LOG(INFO) << "Unable to bound word, giving up.";
        return;
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
      stack->PushFront(NewGotoPositionTransformation(initial_position));
      Result current_result;
      NewDeleteCharactersTransformation(characters_to_erase, true)
          ->Apply(editor_state, buffer, &current_result);
      result->modified_buffer |= current_result.modified_buffer;
      stack->PushFront(std::move(current_result.undo));
      if (!current_result.success) {
        result->success = false;
        break;
      }
    }
    result->undo = std::move(stack);
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteWordsTransformation(repetitions_, copy_to_paste_buffer_);
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

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
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
    result->undo = TransformationAtPosition(
        LineColumn(buffer->position().line),
        ComposeTransformation(
            NewInsertBufferTransformation(deleted_text, 1, END),
            NewGotoPositionTransformation(buffer->position())));
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteLinesTransformation(repetitions_, copy_to_paste_buffer_);
  }

 private:
  size_t repetitions_;
  bool copy_to_paste_buffer_;
};

class DeleteTransformation : public Transformation {
 public:
  DeleteTransformation(EditorState::Structure structure, size_t repetitions,
                       bool copy_to_paste_buffer)
      : structure_(structure),
        repetitions_(repetitions),
        copy_to_paste_buffer_(copy_to_paste_buffer) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    unique_ptr<Transformation> delegate;
    switch (structure_) {
      case EditorState::CHAR:
        delegate = NewDeleteCharactersTransformation(
            repetitions_, copy_to_paste_buffer_);
        break;
      case EditorState::WORD:
        delegate =
            NewDeleteWordsTransformation(repetitions_, copy_to_paste_buffer_);
        break;
      case EditorState::LINE:
        delegate =
            NewDeleteLinesTransformation(repetitions_, copy_to_paste_buffer_);
        break;
      default:
        LOG(INFO) << "DeleteTransformation can't handle structure: "
                  << structure_;
        delegate = NewNoopTransformation();
    }
    return delegate->Apply(editor_state, buffer, result);
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteTransformation(
        structure_, repetitions_, copy_to_paste_buffer_);
  }

 private:
  EditorState::Structure structure_;
  size_t repetitions_;
  bool copy_to_paste_buffer_;
};

}  // namespace

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
    EditorState::Structure structure, size_t repetitions,
    bool copy_to_paste_buffer) {
  return unique_ptr<Transformation>(
      new DeleteTransformation(structure, repetitions, copy_to_paste_buffer));
}

}  // namespace editor
}  // namespace afc