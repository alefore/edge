#include <memory>
#include <map>

#include "advanced_mode.h"
#include "command.h"
#include "command_mode.h"
#include "editor.h"
#include "help_command.h"
#include "map_mode.h"

namespace afc {
namespace editor {

using std::make_pair;
using std::map;
using std::shared_ptr;
using std::unique_ptr;

class CloseCurrentBuffer : public Command {
  const string Description() {
    return "closes the current buffer";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    editor_state->buffers.erase(editor_state->current_buffer);
    editor_state->current_buffer = editor_state->buffers.begin();
    editor_state->mode = std::move(NewCommandMode());
    editor_state->repetitions = 1;
  }
};

static const map<int, Command*>& GetAdvancedModeMap() {
  static map<int, Command*> output;
  if (output.empty()) {
    output.insert(make_pair('c', new CloseCurrentBuffer()));
    output.insert(make_pair('?', NewHelpCommand(output, "advance command mode").release()));
  }
  return output;
}

unique_ptr<EditorMode> NewAdvancedMode() {
  unique_ptr<MapMode> mode(new MapMode(GetAdvancedModeMap()));
  return std::move(mode);
}

}  // namespace afc
}  // namespace editor
