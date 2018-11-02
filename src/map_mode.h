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
  MapMode(std::shared_ptr<EditorMode> delegate);

  void ProcessInput(wint_t c, EditorState* editor_state) override;

  // Adds an entry mapping a given string to a given command.
  void Add(wstring name, Command* value);

 private:
  static void Populate(const MapMode* input, std::vector<const Map*>* output);

  vector<wint_t> current_input_;
  Map commands_;
  std::shared_ptr<EditorMode> const delegate_;
};

}  // namespace editor
}  // namespace afc

#endif
