#ifndef __AFC_EDITOR_MAP_MODE_H__
#define __AFC_EDITOR_MAP_MODE_H__

#include <list>
#include <map>
#include <memory>
#include <string>

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

  std::map<wstring, Command*> Coallesce() const;

  // Adds an entry mapping a given string to a given command.
  void Add(wstring name, std::unique_ptr<Command> value);
  void Add(wstring name, std::unique_ptr<vm::Value> value,
           vm::Environment* environment);
  void Add(wstring name, std::function<void()> value, wstring description);

 private:
  friend class MapMode;
  std::list<std::shared_ptr<std::map<std::wstring, std::unique_ptr<Command>>>>
      commands_;
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
