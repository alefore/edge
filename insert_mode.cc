#include "insert_mode.h"

#include <cassert>
#include <memory>

#include "command_mode.h"
#include "editable_string.h"
#include "editor.h"
#include "lazy_string_append.h"
#include "substring.h"
#include "terminal.h"

namespace {
using namespace afc::editor;

class InsertMode : public EditorMode {
 public:
  InsertMode(shared_ptr<EditableString>& line) : line_(line) {}

  void ProcessInput(int c, EditorState* editor_state) {
    auto buffer = editor_state->get_current_buffer();
    buffer->MaybeAdjustPositionCol();
    switch (c) {
      case Terminal::ESCAPE:
        editor_state->mode = std::move(NewCommandMode());
        editor_state->repetitions = 1;
        return;
      case Terminal::BACKSPACE:
        if (line_->Backspace()) {
          buffer->set_modified(true);
          editor_state->screen_needs_redraw = true;
          buffer->set_current_position_col(buffer->current_position_col() - 1);
        } else if (buffer->current_position_col() == 0) {
          // Join lines.
          if (buffer->current_position_line() == 0)
            return;

          shared_ptr<LazyString> old_line = buffer->current_line()->contents;
          buffer->contents()->erase(
              buffer->contents()->begin() + buffer->current_position_line());
          buffer->set_modified(true);

          buffer->set_current_position_line(buffer->current_position_line() - 1);
          auto prefix = buffer->current_line()->contents;
          line_ = EditableString::New(StringAppend(prefix, old_line), prefix->size());
          buffer->set_current_position_col(prefix->size());
          buffer->current_line()->contents = line_;
          editor_state->screen_needs_redraw = true;
        } else {
          auto prefix = Substring(
              buffer->current_line()->contents, 0,
              min(buffer->current_position_col(),
                  buffer->current_line()->contents->size()));
          line_ = EditableString::New(
              Substring(buffer->current_line()->contents,
                        buffer->current_position_col()),
              0,
              prefix->ToString());
          buffer->current_line()->contents = line_;
          assert(line_->Backspace());
          buffer->set_modified(true);
          editor_state->screen_needs_redraw = true;
          buffer->set_current_position_col(buffer->current_position_col() - 1);
        }
        return;
      case '\n':
        size_t pos = buffer->current_position_col();

        // Adjust the old line.
        buffer->current_line()->contents =
            Substring(buffer->current_line()->contents, 0, pos);

        // Create a new line and insert it.
        line_ = EditableString::New(Substring(line_, pos), 0);

        shared_ptr<Line> line(new Line());
        line->contents = line_;
        buffer->contents()->insert(
            buffer->contents()->begin() + buffer->current_position_line() + 1,
            line);
        buffer->set_modified(true);

        // Move to the new line and schedule a redraw.
        buffer->set_current_position_line(buffer->current_position_line() + 1);
        buffer->set_current_position_col(0);
        editor_state->screen_needs_redraw = true;
        return;
    }
    line_->Insert(c);
    buffer->set_modified(true);
    editor_state->screen_needs_redraw = true;
    buffer->set_current_position_col(buffer->current_position_col() + 1);
  }

 private:
  shared_ptr<EditableString> line_;
};

}  // namespace

namespace afc {
namespace editor {

using std::unique_ptr;
using std::shared_ptr;

void EnterInsertCharactersMode(EditorState* editor_state) {
  editor_state->status = "";
  auto buffer = editor_state->get_current_buffer();
  shared_ptr<EditableString> new_line;
  editor_state->PushCurrentPosition();
  if (buffer->contents()->empty()) {
    new_line = EditableString::New("");
    buffer->AppendLine(new_line);
  } else {
    buffer->MaybeAdjustPositionCol();
    auto line = buffer->current_line();
    new_line = EditableString::New(
        line->contents, buffer->current_position_col());
    line->contents = new_line;
  }
  editor_state->mode.reset(new InsertMode(new_line));
}

void EnterInsertMode(EditorState* editor_state) {
  if (editor_state->current_buffer == editor_state->buffers.end()) {
    shared_ptr<OpenBuffer> buffer(new OpenBuffer);
    editor_state->buffers.insert(make_pair("[anonymous buffer]", buffer));
    editor_state->current_buffer = editor_state->buffers.begin();
  }

  editor_state->status = "";
  if (editor_state->structure == 0) {
    EnterInsertCharactersMode(editor_state);
  } else if (editor_state->structure == 1) {
    auto buffer = editor_state->get_current_buffer();
    shared_ptr<Line> line(new Line());
    line->contents = EmptyString();
    if (editor_state->direction == BACKWARDS) {
      buffer->set_current_position_line(buffer->current_position_line() + 1);
    }
    buffer->contents()->insert(
        buffer->contents()->begin() + buffer->current_position_line(),
        line);
    EnterInsertCharactersMode(editor_state);
    editor_state->screen_needs_redraw = true;
  }
  editor_state->ResetStructure();
}

}  // namespace afc
}  // namespace editor
