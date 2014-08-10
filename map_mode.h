#ifndef __AFC_EDITOR_MAP_MODE_H__
#define __AFC_EDITOR_MAP_MODE_H__

#include <memory>
#include <map>

#include "editor_mode.h"
#include "command.h"

namespace afc {
namespace editor {

using std::map;
using std::unique_ptr;

class MapMode : public EditorMode {
 public:
  MapMode(const map<int, Command*>& commands);

  void ProcessInput(int c, EditorState* editor_state);

 private:
  const map<int, Command*>& commands_;
};

}  // namespace editor
}  // namespace afc

#endif
