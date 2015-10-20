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
  // Adds to output an entry mapping a given string to a given command.
  static void RegisterEntry(wstring name, Command* value,
                            map<vector<wint_t>, Command*>* output);

  MapMode(map<vector<wint_t>, Command*> commands,
          Command* default_command);

  void ProcessInput(wint_t c, EditorState* editor_state);

 private:
  vector<wint_t> current_input_;
  const map<vector<wint_t>, Command*> commands_;
  Command* const default_command_;
};

}  // namespace editor
}  // namespace afc

#endif
