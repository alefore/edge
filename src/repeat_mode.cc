#include "src/repeat_mode.h"

#include <memory>

#include "src/command_mode.h"
#include "src/editor.h"

namespace {
using namespace afc::editor;

class RepeatMode : public EditorMode {
 public:
  RepeatMode(std::function<void(EditorState*, int)> consumer)
      : consumer_(consumer), result_(0) {}

  void ProcessInput(wint_t c, EditorState* editor_state) {
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) {
      return;
    }
    if (c < '0' || c > '9') {
      consumer_(editor_state, result_);
      buffer->ResetMode();
      buffer->mode()->ProcessInput(c, editor_state);
      return;
    }
    result_ = 10 * result_ + c - '0';
    consumer_(editor_state, result_);
  }

 private:
  function<void(EditorState*, int)> consumer_;
  int result_;
};
}  // namespace

namespace afc {
namespace editor {

std::unique_ptr<EditorMode> NewRepeatMode(
    std::function<void(EditorState*, int)> consumer) {
  return std::make_unique<RepeatMode>(std::move(consumer));
}

}  // namespace editor
}  // namespace afc
