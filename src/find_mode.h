#ifndef __AFC_EDITOR_FIND_MODE_H__
#define __AFC_EDITOR_FIND_MODE_H__

#include <memory>

#include "src/command.h"
#include "src/editor.h"

namespace afc::editor {
std::unique_ptr<Command> NewFindModeCommand();
}  // namespace afc::editor

#endif
