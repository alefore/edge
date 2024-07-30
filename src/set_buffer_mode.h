#ifndef __AFC_EDITOR_SET_BUFFER_MODE_H__
#define __AFC_EDITOR_SET_BUFFER_MODE_H__

#include <memory>
#include <optional>

#include "src/command_argument_mode.h"
#include "src/editor_mode.h"

namespace afc::editor {
std::optional<language::gc::Root<InputReceiver>> NewSetBufferMode(
    EditorState& editor);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_SET_BUFFER_MODE_H__
