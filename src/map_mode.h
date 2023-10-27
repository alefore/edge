#ifndef __AFC_EDITOR_MAP_MODE_H__
#define __AFC_EDITOR_MAP_MODE_H__

#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "src/command.h"
#include "src/editor_mode.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/environment.h"
#include "src/vm/value.h"

namespace afc::editor {

class MapMode;
class EditorState;

class MapModeCommands {
  struct ConstructorAccessTag {};

  struct Frame {
    std::map<std::wstring, language::gc::Ptr<Command>> commands;

    // The key is the name of a variable. The set contains all commands
    // associated with that variable.
    std::unordered_map<std::wstring, std::unordered_set<std::wstring>>
        variable_commands;
  };

  EditorState& editor_state_;
  std::list<language::NonNull<std::shared_ptr<Frame>>> frames_;

 public:
  static language::gc::Root<MapModeCommands> New(EditorState& editor_state);

  MapModeCommands(ConstructorAccessTag, EditorState& editor_state);
  language::gc::Root<MapModeCommands> NewChild();

  // Flattens the set of commands (in the entire list), grouped by category (as
  // the key of the outer map).
  std::map<std::wstring, std::map<std::wstring, language::NonNull<Command*>>>
  Coallesce() const;

  // Adds an entry mapping a given string to a given command.
  void Add(std::wstring name,
           language::NonNull<std::unique_ptr<Command>> value);
  void Add(std::wstring name, std::wstring description,
           language::gc::Root<vm::Value> value,
           language::gc::Root<vm::Environment> environment);
  void Add(std::wstring name, std::function<void()> value,
           std::wstring description);

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const;

 private:
  friend class MapMode;
};

class MapMode : public EditorMode {
  struct ConstructorAccessTag {};

  std::wstring current_input_;
  const language::gc::Ptr<MapModeCommands> commands_;

 public:
  static language::gc::Root<MapMode> New(
      language::gc::Pool& pool, language::gc::Ptr<MapModeCommands> commands);

  MapMode(ConstructorAccessTag, language::gc::Ptr<MapModeCommands> commands);

  void ProcessInput(wint_t c) override;
  CursorMode cursor_mode() const override;

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const override;
};

}  // namespace afc::editor

#endif
