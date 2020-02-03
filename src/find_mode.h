#ifndef __AFC_EDITOR_FIND_MODE_H__
#define __AFC_EDITOR_FIND_MODE_H__

#include <memory>

#include "src/command.h"
#include "src/editor.h"

namespace afc::editor {
std::unique_ptr<Command> NewFindModeCommand(Direction initial_direction);
}  // namespace afc::editor

#endif
