#ifndef __AFC_EDITOR_MAP_MODE_H__
#define __AFC_EDITOR_MAP_MODE_H__

#include <memory>
#include <map>
#include <string>
#include <vector>

#include "editor_mode.h"
#include "command.h"

namespace afc {
namespace editor {

using std::map;
using std::unique_ptr;
using std::vector;
using std::wstring;

class MapMode : public EditorMode {
 public:
  using Map = map<vector<wint_t>, Command*>;
  // Adds to output an entry mapping a given string to a given command.
  static void RegisterEntry(wstring name, Command* value, Map* output);

  MapMode(std::shared_ptr<const Map> commands, Command* default_command);

  void ProcessInput(wint_t c, EditorState* editor_state);

 private:
  vector<wint_t> current_input_;
  const std::shared_ptr<const Map> commands_;
  Command* const default_command_;
};

}  // namespace editor
}  // namespace afc

#endif
