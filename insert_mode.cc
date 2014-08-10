#include "insert_mode.h"

#include <memory>

#include "command_mode.h"
#include "find_mode.h"
#include "terminal.h"
#include "editor.h"

namespace {
using namespace afc::editor;
class EditableString : public LazyString {
 public:
  EditableString(const shared_ptr<LazyString>& base, size_t position)
      : base_(base), position_(position) {}

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
    if (c == -1) {
      editor_state->mode = std::move(NewCommandMode());
      editor_state->repetitions = 1;
      return;
    }
    if (c == 127) {
      if (line_->Backspace()) {
        editor_state->screen_needs_redraw = true;
        editor_state->get_current_buffer()->current_position_col --;
      }
      return;
    }
    line_->Insert(c);
    editor_state->screen_needs_redraw = true;
    editor_state->get_current_buffer()->current_position_col ++;
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
  auto line(buffer->current_line());
  shared_ptr<EditableString> new_line(
      new EditableString(line->contents, buffer->current_position_col));
  line->contents = new_line;
  editor_state->mode.reset(new InsertMode(new_line));
}

}  // namespace afc
}  // namespace editor
