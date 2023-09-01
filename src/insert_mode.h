#ifndef __AFC_EDITOR_INSERT_MODE_H__
#define __AFC_EDITOR_INSERT_MODE_H__

#include <memory>

#include "src/futures/delete_notification.h"
#include "src/futures/futures.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"

namespace afc::editor {

class EditorState;
class OpenBuffer;
class Command;

language::NonNull<std::unique_ptr<Command>> NewFindCompletionCommand(
    EditorState& editor_state);

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
  static language::NonNull<std::unique_ptr<ScrollBehaviorFactory>> Default();
  virtual ~ScrollBehaviorFactory() = default;
  virtual futures::Value<language::NonNull<std::unique_ptr<ScrollBehavior>>>
  Build(futures::DeleteNotification::Value abort_value) = 0;
};

struct InsertModeOptions {
  EditorState& editor_state;

  // The buffers to insert into. If absent, defaults to the active buffers.
  std::optional<std::vector<language::gc::Root<OpenBuffer>>> buffers =
      std::nullopt;

  // The buffer with the model to use for completion. All lines will be sorted
  // and each line should be of the form "input output", where "input" is a
  // short sequence of alphabetic characters that, when inserted, will be
  // expanded to "output".
  futures::ListenableValue<language::gc::Root<OpenBuffer>> completion_model;

  // Optional function to run whenever the contents of a buffer are modified. If
  // `buffers` contains multiple buffers, this will be called multiple times
  // (once for each buffer each time it is modified).
  std::function<futures::Value<language::EmptyValue>(OpenBuffer&)>
      modify_handler = nullptr;

  language::NonNull<std::shared_ptr<ScrollBehaviorFactory>> scroll_behavior =
      ScrollBehaviorFactory::Default();

  // Optional function to run when escape is pressed (and thus insert mode is
  // exited). Defaults to resetting the mode back to the default.
  std::function<void()> escape_handler = nullptr;

  // Optional function to run when a new line is received. Defaults to inserting
  // a new line and moving to it.
  std::function<futures::Value<language::EmptyValue>(OpenBuffer&)>
      new_line_handler = nullptr;

  // Optional function to run when the user presses Tab for completions. Returns
  // true if completions are being attempted; false if autocompletion is not
  // enabled.
  std::function<bool(OpenBuffer&)> start_completion = nullptr;
};

void EnterInsertMode(InsertModeOptions options);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_INSERT_MODE_H__
