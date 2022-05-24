#include "src/repeat_mode.h"

#include <memory>

#include "src/command_mode.h"
#include "src/editor.h"

namespace afc::editor {
namespace {
class RepeatMode : public EditorMode {
 public:
  RepeatMode(EditorState& editor_state, std::function<void(int)> consumer)
      : editor_state_(editor_state), consumer_(consumer), result_(0) {}

  void ProcessInput(wint_t c) {
    if (c < '0' || c > '9') {
      consumer_(result_);
      auto old_mode_to_keep_this_alive =
          editor_state_.set_keyboard_redirect(nullptr);
      editor_state_.ProcessInput(c);
      return;
    }
    result_ = 10 * result_ + c - '0';
    consumer_(result_);
  }

  CursorMode cursor_mode() const { return CursorMode::kDefault; }

 private:
  EditorState& editor_state_;
  const std::function<void(int)> consumer_;
  int result_;
};
}  // namespace

std::unique_ptr<EditorMode> NewRepeatMode(EditorState& editor_state,
                                          std::function<void(int)> consumer) {
  return std::make_unique<RepeatMode>(editor_state, std::move(consumer));
}
}  // namespace afc::editor
