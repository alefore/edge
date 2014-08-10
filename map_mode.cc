#include <memory>

#include "map_mode.h"

namespace afc {
namespace editor {

struct EditorState;

MapMode::MapMode(const map<int, Command*>& commands)
    : commands_(commands) {}

void MapMode::ProcessInput(int c, EditorState* editor_state) {
  auto it = commands_.find(c);
  if (it == commands_.end()) {
    return;
  }
  it->second->ProcessInput(c, editor_state);
}

}  // namespace editor
}  // namespace afc
