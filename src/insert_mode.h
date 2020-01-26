#ifndef __AFC_EDITOR_INSERT_MODE_H__
#define __AFC_EDITOR_INSERT_MODE_H__

#include <memory>

#include "command.h"
#include "editor.h"

namespace afc::editor {

std::unique_ptr<Command> NewFindCompletionCommand();

class ScrollBehavior {
 public:
  virtual ~ScrollBehavior() = default;
  virtual void Up(EditorState* editor_state, OpenBuffer* buffer) = 0;
  virtual void Down(EditorState* editor_state, OpenBuffer* buffer) = 0;
  virtual void Left(EditorState* editor_state, OpenBuffer* buffer) = 0;
  virtual void Right(EditorState* editor_state, OpenBuffer* buffer) = 0;
  virtual void Begin(EditorState* editor_state, OpenBuffer* buffer) = 0;
  virtual void End(EditorState* editor_state, OpenBuffer* buffer) = 0;
};

class DefaultScrollBehavior : public ScrollBehavior {
 public:
  DefaultScrollBehavior() = default;
  void Up(EditorState* editor_state, OpenBuffer* buffer) override;
  void Down(EditorState* editor_state, OpenBuffer* buffer) override;
  void Left(EditorState* editor_state, OpenBuffer* buffer) override;
  void Right(EditorState* editor_state, OpenBuffer* buffer) override;
  void Begin(EditorState* editor_state, OpenBuffer* buffer) override;
  void End(EditorState* editor_state, OpenBuffer* buffer) override;
};

class ScrollBehaviorFactory {
 public:
  static std::unique_ptr<ScrollBehaviorFactory> Default();
  virtual ~ScrollBehaviorFactory() = default;
  virtual std::unique_ptr<ScrollBehavior> Build() = 0;
};

struct InsertModeOptions {
  EditorState* editor_state = nullptr;

  // The buffers to insert into. If absent, defaults to the active buffers.
  std::optional<std::vector<std::shared_ptr<OpenBuffer>>> buffers;

  // Optional function to run whenever the contents of the buffer are modified.
  std::function<futures::Value<bool>(const std::shared_ptr<OpenBuffer>&)>
      modify_handler;

  std::shared_ptr<ScrollBehaviorFactory> scroll_behavior =
      ScrollBehaviorFactory::Default();

  // Optional function to run when escape is pressed (and thus insert mode is
  // exited). Defaults to resetting the mode back to the default.
  std::function<void()> escape_handler;

  // Optional function to run when a new line is received. Defaults to inserting
  // a new line and moving to it.
  std::function<futures::Value<bool>(const std::shared_ptr<OpenBuffer>&)>
      new_line_handler;

  // Optional function to run when the user presses Tab for completions. Returns
  // true if completions are being attempted; false if autocompletion is not
  // enabled.
  std::function<bool(const std::shared_ptr<OpenBuffer>&)> start_completion;
};

void EnterInsertMode(InsertModeOptions options);

}  // namespace afc::editor

#endif
