#ifndef __AFC_EDITOR_INSERT_MODE_H__
#define __AFC_EDITOR_INSERT_MODE_H__

#include <memory>

#include "command.h"
#include "editor.h"

namespace afc {
namespace editor {

using std::unique_ptr;

std::unique_ptr<Command> NewFindCompletionCommand();

class ScrollBehavior {
 public:
  static shared_ptr<const ScrollBehavior> Default();
  virtual ~ScrollBehavior() = default;
  virtual void Up(EditorState* editor_state, OpenBuffer* buffer) const = 0;
  virtual void Down(EditorState* editor_state, OpenBuffer* buffer) const = 0;
  virtual void Left(EditorState* editor_state, OpenBuffer* buffer) const = 0;
  virtual void Right(EditorState* editor_state, OpenBuffer* buffer) const = 0;
  virtual void Begin(EditorState* editor_state, OpenBuffer* buffer) const = 0;
  virtual void End(EditorState* editor_state, OpenBuffer* buffer) const = 0;
};

struct InsertModeOptions {
  EditorState* editor_state = nullptr;

  // The buffer to insert into. If nullptr, defaults to the current buffer.
  shared_ptr<OpenBuffer> buffer;

  // Optional function to run whenever the contents of the buffer are modified.
  std::function<void()> modify_listener;

  shared_ptr<const ScrollBehavior> scroll_behavior = ScrollBehavior::Default();

  // Optional function to run when escape is pressed (and thus insert mode is
  // exited). Defaults to resetting the mode back to the default.
  std::function<void()> escape_handler;

  // Optional function to run when a new line is received. Defaults to inserting
  // a new line and moving to it.
  std::function<void()> new_line_handler;

  // Optional function to run when the user presses Tab for completions. Returns
  // true if completions are being attempted; false if autocompletion is not
  // enabled.
  std::function<bool()> start_completion;
};

// Default insert mode.
void EnterInsertMode(EditorState* editor_state);

void EnterInsertMode(InsertModeOptions options);

}  // namespace editor
}  // namespace afc

#endif
