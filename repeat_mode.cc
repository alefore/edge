#include "repeat_mode.h"

#include <memory>

#include "command_mode.h"
#include "editor.h"

namespace {
using namespace afc::editor;

class RepeatMode : public EditorMode {
 public:
  RepeatMode(function<void(int, EditorState*, int)> done)
      : done_(done), result_(0) {}

  void ProcessInput(int c, EditorState* editor_state) {
    if (c < '0' || c > '9') {
      done_(c, editor_state, result_);
      return;
    }
    result_ = 10 * result_ + c - '0';
  }
 private:
  function<void(int, EditorState*, int)> done_;
  int result_;
};
}

namespace afc {
namespace editor {

using std::unique_ptr;

unique_ptr<EditorMode> NewRepeatMode(function<void(int, EditorState*, int)> done) {
  return unique_ptr<EditorMode>(new RepeatMode(done));
}

}  // namespace afc
}  // namespace editor
