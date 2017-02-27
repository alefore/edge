#include "repeat_mode.h"

#include <memory>

#include "command_mode.h"
#include "editor.h"

namespace {
using namespace afc::editor;

class RepeatMode : public EditorMode {
 public:
  RepeatMode(function<void(EditorState*, int)> consumer)
      : consumer_(consumer), result_(0) {}

  void ProcessInput(wint_t c, EditorState* editor_state) {
    if (c < '0' || c > '9') {
      consumer_(editor_state, result_);
      editor_state->ResetMode();
      editor_state->mode()->ProcessInput(c, editor_state);
      return;
    }
    result_ = 10 * result_ + c - '0';
    consumer_(editor_state, result_);
  }
 private:
  function<void(EditorState*, int)> consumer_;
  int result_;
};
}

namespace afc {
namespace editor {

using std::unique_ptr;

unique_ptr<EditorMode> NewRepeatMode(
    function<void(EditorState*, int)> consumer) {
  return unique_ptr<EditorMode>(new RepeatMode(consumer));
}

}  // namespace afc
}  // namespace editor
