#ifndef __AFC_EDITOR_SET_MODE_COMMAND_H__
#define __AFC_EDITOR_SET_MODE_COMMAND_H__

#include <functional>
#include <memory>
#include <string>

#include "src/language/safe_types.h"

namespace afc::editor {
class EditorState;
class EditorMode;
class Command;
struct SetModeCommandOptions {
  EditorState& editor_state;
  std::wstring description;
  std::wstring category;
  std::function<std::unique_ptr<EditorMode>()> factory;
};

language::NonNull<std::unique_ptr<Command>> NewSetModeCommand(
    SetModeCommandOptions options);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_SET_MODE_COMMAND_H__
