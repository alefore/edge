#ifndef __AFC_EDITOR_INSERT_MODE_H__
#define __AFC_EDITOR_INSERT_MODE_H__

#include <memory>

#include "src/command.h"
#include "src/editor.h"
#include "src/futures/futures.h"

namespace afc::editor {

std::unique_ptr<Command> NewFindCompletionCommand(EditorState& editor_state);

class ScrollBehavior {
 public:
  virtual ~ScrollBehavior() = default;
  virtual void Up(OpenBuffer& buffer) = 0;
  virtual void Down(OpenBuffer& buffer) = 0;
  virtual void Left(OpenBuffer& buffer) = 0;
  virtual void Right(OpenBuffer& buffer) = 0;
  virtual void Begin(OpenBuffer& buffer) = 0;
  virtual void End(OpenBuffer& buffer) = 0;
};

class DefaultScrollBehavior : public ScrollBehavior {
 public:
  DefaultScrollBehavior() = default;
  void Up(OpenBuffer& buffer) override;
  void Down(OpenBuffer& buffer) override;
  void Left(OpenBuffer& buffer) override;
  void Right(OpenBuffer& buffer) override;
  void Begin(OpenBuffer& buffer) override;
  void End(OpenBuffer& buffer) override;
};

class ScrollBehaviorFactory {
 public:
  static std::unique_ptr<ScrollBehaviorFactory> Default();
  virtual ~ScrollBehaviorFactory() = default;
  virtual futures::Value<std::unique_ptr<ScrollBehavior>> Build(
      std::shared_ptr<Notification> abort_notification) = 0;
};

struct InsertModeOptions {
  EditorState& editor_state;

  // The buffers to insert into. If absent, defaults to the active buffers.
  std::optional<std::vector<std::shared_ptr<OpenBuffer>>> buffers =
      std::nullopt;

  // Optional function to run whenever the contents of the buffer are modified.
  std::function<futures::Value<EmptyValue>(const std::shared_ptr<OpenBuffer>&)>
      modify_handler = nullptr;

  std::shared_ptr<ScrollBehaviorFactory> scroll_behavior =
      ScrollBehaviorFactory::Default();

  // Optional function to run when escape is pressed (and thus insert mode is
  // exited). Defaults to resetting the mode back to the default.
  std::function<void()> escape_handler = nullptr;

  // Optional function to run when a new line is received. Defaults to inserting
  // a new line and moving to it.
  std::function<futures::Value<EmptyValue>(const std::shared_ptr<OpenBuffer>&)>
      new_line_handler = nullptr;

  // Optional function to run when the user presses Tab for completions. Returns
  // true if completions are being attempted; false if autocompletion is not
  // enabled.
  std::function<bool(const std::shared_ptr<OpenBuffer>&)> start_completion =
      nullptr;
};

void EnterInsertMode(InsertModeOptions options);

}  // namespace afc::editor

#endif
