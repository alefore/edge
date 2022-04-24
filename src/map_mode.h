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
#include "src/language/safe_types.h"
#include "vm/public/environment.h"
#include "vm/public/value.h"

namespace afc::editor {

class MapMode;
class EditorState;

class MapModeCommands {
 public:
  MapModeCommands(EditorState& editor_state);
  std::unique_ptr<MapModeCommands> NewChild();

  // Flattens the set of commands (in the entire list), grouped by category (as
  // the key of the outer map).
  std::map<std::wstring, std::map<std::wstring, Command*>> Coallesce() const;

  // Adds an entry mapping a given string to a given command.
  void Add(std::wstring name, std::unique_ptr<Command> value);
  void Add(std::wstring name, std::wstring description,
           language::NonNull<std::unique_ptr<vm::Value>> value,
           std::shared_ptr<vm::Environment> environment);
  void Add(std::wstring name, std::function<void()> value,
           std::wstring description);

 private:
  friend class MapMode;

  struct Frame {
    std::map<std::wstring, std::unique_ptr<Command>> commands;
    // The key is the name of a variable. The set contains all commands
    // associated with that variable.
    std::unordered_map<std::wstring, std::unordered_set<std::wstring>>
        variable_commands;
  };

  EditorState& editor_state_;
  std::list<std::shared_ptr<Frame>> frames_;
};

class MapMode : public EditorMode {
 public:
  MapMode(std::shared_ptr<MapModeCommands> commands);

  void ProcessInput(wint_t c) override;
  CursorMode cursor_mode() const override;

 private:
  std::wstring current_input_;
  std::shared_ptr<MapModeCommands> commands_;
};

}  // namespace afc::editor

#endif
