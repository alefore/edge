#include <memory>

#include "map_mode.h"

namespace afc {
namespace editor {

struct EditorState;

void MapMode::RegisterEntry(wstring name, Command* value,
                            map<vector<wint_t>, Command*>* output) {
  vector<wint_t> key;
  for (wchar_t c : name) {
    key.push_back(c);
  }
  output->insert(make_pair(std::move(key), value));
}

MapMode::MapMode(const map<vector<wint_t>, Command*>& commands,
                 Command* default_command)
    : commands_(commands),
      default_command_(default_command) {}

void MapMode::ProcessInput(wint_t c, EditorState* editor_state) {
  vector<wint_t> input = current_input_;
  input.push_back(c);
  auto it = commands_.lower_bound(input);
  if (it == commands_.end()
      || !std::equal(input.begin(), input.end(), it->first.begin())) {
    default_command_->ProcessInput(c, editor_state);
    return;
  }
  if (input != it->first) {
    current_input_ = std::move(input);
    return;
  }

  current_input_ = {};
  it->second->ProcessInput(c, editor_state);
}

}  // namespace editor
}  // namespace afc
