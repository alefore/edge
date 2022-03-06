#include "src/repeat_mode.h"

#include <memory>

#include "src/command_mode.h"
#include "src/editor.h"

namespace {
using namespace afc::editor;

class RepeatMode : public EditorMode {
 public:
  RepeatMode(std::function<void(int)> consumer)
      : consumer_(consumer), result_(0) {}

  void ProcessInput(wint_t c, EditorState* editor_state) {
    if (c < '0' || c > '9') {
      consumer_(result_);
      editor_state->set_keyboard_redirect(nullptr);
      editor_state->ProcessInput(c);
      return;
    }
    result_ = 10 * result_ + c - '0';
    consumer_(result_);
  }

  CursorMode cursor_mode() const { return CursorMode::kDefault; }

 private:
  std::function<void(int)> consumer_;
  int result_;
};
}  // namespace

namespace afc {
namespace editor {

std::unique_ptr<EditorMode> NewRepeatMode(std::function<void(int)> consumer) {
  return std::make_unique<RepeatMode>(std::move(consumer));
}

}  // namespace editor
}  // namespace afc
