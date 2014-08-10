#include <memory>
#include <string>

#include "command_mode.h"
#include "search_mode.h"
#include "editor.h"

namespace {

using std::unique_ptr;
using std::shared_ptr;

class SearchMode : public EditorMode {

  void ProcessInput(int c, EditorState* editor_state) {
    for (int times = 0; times < editor_state->repetitions; times++) {
      if (!SeekOnce(editor_state->get_current_buffer(), c)) {
        break;
      }
    }
    editor_state->mode = std::move(NewCommandMode());
    editor_state->repetitions = 1;
  }
};

}

namespace afc {
namespace editor {

unique_ptr<EditorMode> NewSearchMode() {
  return std::move(unique_ptr<EditorMode>(new PromptMode("/", new SearchCommand)));
}

}  // namespace afc
}  // namespace editor
