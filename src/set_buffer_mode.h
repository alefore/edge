#ifndef __AFC_EDITOR_SET_BUFFER_MODE_H__
#define __AFC_EDITOR_SET_BUFFER_MODE_H__

#include <memory>

#include "src/command_argument_mode.h"
#include "src/editor_mode.h"

namespace afc::editor {
std::unique_ptr<EditorMode> NewSetBufferMode(EditorState* editor);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_SET_BUFFER_MODE_H__
