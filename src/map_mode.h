#ifndef __AFC_EDITOR_MAP_MODE_H__
#define __AFC_EDITOR_MAP_MODE_H__

#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "command.h"
#include "editor_mode.h"
#include "vm/public/environment.h"
#include "vm/public/value.h"

namespace afc {
namespace editor {

using std::map;
using std::unique_ptr;
using std::wstring;

class MapMode;

class MapModeCommands {
 public:
  MapModeCommands();
  std::unique_ptr<MapModeCommands> NewChild();

  // Flattens the set of commands (in the entire list), grouped by category (as
  // the key of the outer map).
  std::map<wstring, std::map<wstring, Command*>> Coallesce() const;

  // Adds an entry mapping a given string to a given command.
  void Add(wstring name, std::unique_ptr<Command> value);
  void Add(wstring name, wstring description, std::unique_ptr<vm::Value> value,
           std::shared_ptr<vm::Environment> environment);
  void Add(wstring name, std::function<void(EditorState*)> value,
           wstring description);

 private:
  friend class MapMode;

  struct Frame {
    std::map<std::wstring, std::unique_ptr<Command>> commands;
    // The key is the name of a variable. The set contains all commands
    // associated with that variable.
    std::unordered_map<wstring, std::unordered_set<wstring>> variable_commands;
  };

  std::list<std::shared_ptr<Frame>> frames_;
};

class MapMode : public EditorMode {
 public:
  MapMode(std::shared_ptr<MapModeCommands> commands);

  void ProcessInput(wint_t c, EditorState* editor_state) override;

 private:
  wstring current_input_;
  std::shared_ptr<MapModeCommands> commands_;
};

}  // namespace editor
}  // namespace afc

#endif
