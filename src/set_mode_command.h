#ifndef __AFC_EDITOR_SET_MODE_COMMAND_H__
#define __AFC_EDITOR_SET_MODE_COMMAND_H__

#include <functional>
#include <memory>
#include <string>

#include "src/language/gc.h"
#include "src/language/lazy_string/lazy_string.h"

namespace afc::editor {
class EditorState;
class EditorMode;
class Command;
struct SetModeCommandOptions {
  EditorState& editor_state;
  language::lazy_string::LazyString description;
  language::lazy_string::LazyString category;
  std::function<std::unique_ptr<EditorMode>()> factory;
};

language::gc::Root<Command> NewSetModeCommand(SetModeCommandOptions options);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_SET_MODE_COMMAND_H__
