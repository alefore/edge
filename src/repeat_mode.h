#ifndef __AFC_EDITOR_REPEAT_MODE_H__
#define __AFC_EDITOR_REPEAT_MODE_H__

#include <functional>

#include "src/language/gc.h"

namespace afc::editor {
class InputReceiver;
class EditorState;
language::gc::Root<InputReceiver> NewRepeatMode(
    EditorState& editor_state, std::function<void(int)> consumer);
}  // namespace afc::editor

#endif
