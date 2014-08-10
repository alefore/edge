#include "insert_mode.h"

#include <cassert>
#include <memory>

#include "command_mode.h"
#include "editor.h"
#include "lazy_string_append.h"
#include "substring.h"
#include "terminal.h"

namespace {
using namespace afc::editor;
class EditableString : public LazyString {
 public:
  EditableString(const shared_ptr<LazyString>& base, size_t position)
      : base_(base), position_(position) {
    assert(position_ <= base_->size());
  }

  EditableString(const string& editable_part)
      : base_(EmptyString()), position_(0), editable_part_(editable_part) {
    assert(position_ <= base_->size());
  }

  EditableString(const shared_ptr<LazyString>& base, size_t position,
                 const string& editable_part)
      : base_(base), position_(position), editable_part_(editable_part) {
    assert(position_ <= base_->size());
  }

  virtual char get(size_t pos) const {
    if (pos < position_) {
      return base_->get(pos);
    }
    if (pos - position_ < editable_part_.size()) {
      return editable_part_.at(pos - position_);
    }
    return base_->get(pos - editable_part_.size());
  }

  virtual size_t size() const {
    return base_->size() + editable_part_.size();
  }

  void Insert(int c) {
    assert(c != '\n');
    editable_part_ += static_cast<char>(c);
  }

  bool Backspace() {
    if (editable_part_.empty()) {
      return false;
    }
    editable_part_.resize(editable_part_.size() - 1);
    return true;
  }

 private:
  const shared_ptr<LazyString> base_;
  size_t position_;
  string editable_part_;
};

class InsertMode : public EditorMode {
 public:
  InsertMode(shared_ptr<EditableString>& line) : line_(line) {}

  void ProcessInput(int c, EditorState* editor_state) {
    auto buffer = editor_state->get_current_buffer();
    buffer->MaybeAdjustPositionCol();
    switch (c) {
      case -1:
        editor_state->mode = std::move(NewCommandMode());
        editor_state->repetitions = 1;
        return;
      case 127:
        if (line_->Backspace()) {
          editor_state->screen_needs_redraw = true;
          editor_state->get_current_buffer()->current_position_col --;
        } else if (buffer->current_position_col == 0) {
          // Join lines.
          if (buffer->current_position_line == 0)
            return;

          shared_ptr<LazyString> old_line = buffer->current_line()->contents;
          buffer->contents.erase(
              buffer->contents.begin() + buffer->current_position_line);

          buffer->current_position_line --;
          auto prefix = buffer->current_line()->contents;
          line_.reset(new EditableString(StringAppend(prefix, old_line), prefix->size()));
          buffer->current_position_col = prefix->size();
          buffer->current_line()->contents = line_;
          editor_state->screen_needs_redraw = true;
        } else {
          auto prefix = Substring(
              buffer->current_line()->contents, 0,
              min(buffer->current_position_col,
                  buffer->current_line()->contents->size()));
          line_.reset(new EditableString(
              Substring(buffer->current_line()->contents,
                        buffer->current_position_col),
              0,
              prefix->ToString()));
          buffer->current_line()->contents = line_;
          assert(line_->Backspace());
          editor_state->screen_needs_redraw = true;
          editor_state->get_current_buffer()->current_position_col --;
        }
        return;
      case '\n':
        size_t pos = buffer->current_position_col;

        // Adjust the old line.
        buffer->current_line()->contents =
            Substring(buffer->current_line()->contents, 0, pos);

        // Create a new line and insert it.
        line_.reset(new EditableString(Substring(line_, pos), 0));

        shared_ptr<Line> line(new Line());
        line->contents = line_;
        buffer->contents.insert(
            buffer->contents.begin() + buffer->current_position_line + 1,
            line);

        // Move to the new line and schedule a redraw.
        buffer->current_position_line++;
        buffer->current_position_col = 0;
        editor_state->screen_needs_redraw = true;
        return;
    }
    line_->Insert(c);
    editor_state->screen_needs_redraw = true;
    buffer->current_position_col ++;
  }

 private:
  shared_ptr<EditableString> line_;
};

}  // namespace

namespace afc {
namespace editor {

using std::unique_ptr;
using std::shared_ptr;

void EnterInsertMode(EditorState* editor_state) {
  auto buffer = editor_state->get_current_buffer();
  buffer->MaybeAdjustPositionCol();
  auto line(buffer->current_line());
  shared_ptr<EditableString> new_line(
      new EditableString(line->contents, buffer->current_position_col));
  line->contents = new_line;
  editor_state->mode.reset(new InsertMode(new_line));
}

}  // namespace afc
}  // namespace editor
