#ifndef __AFC_EDITOR_SET_MODE_COMMAND_H__
#define __AFC_EDITOR_SET_MODE_COMMAND_H__

#include <functional>
#include <memory>
#include <string>

#include "src/command.h"
#include "src/editor_mode.h"

namespace afc::editor {

struct SetModeCommandOptions {
  EditorState& editor_state;
  std::wstring description;
  std::wstring category;
  std::function<std::unique_ptr<EditorMode>()> factory;
};

std::unique_ptr<Command> NewSetModeCommand(SetModeCommandOptions options);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_SET_MODE_COMMAND_H__
