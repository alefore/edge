#include <memory>

#include "map_mode.h"

namespace afc {
namespace editor {

struct EditorState;

MapMode::MapMode(const map<wchar_t, Command*>& commands,
                 Command* default_command)
    : commands_(commands),
      default_command_(default_command) {}

void MapMode::ProcessInput(int c, EditorState* editor_state) {
  auto it = commands_.find(c);
  if (it == commands_.end()) {
    default_command_->ProcessInput(c, editor_state);
    return;
  }
  it->second->ProcessInput(c, editor_state);
}

}  // namespace editor
}  // namespace afc
