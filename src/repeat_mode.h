#ifndef __AFC_EDITOR_REPEAT_MODE_H__
#define __AFC_EDITOR_REPEAT_MODE_H__

#include <functional>
#include <memory>

namespace afc::editor {
class EditorMode;
class EditorState;
std::unique_ptr<EditorMode> NewRepeatMode(EditorState& editor_state,
                                          std::function<void(int)> consumer);
}  // namespace afc::editor

#endif
