#ifndef __AFC_EDITOR_SEARCH_COMMAND_H__
#define __AFC_EDITOR_SEARCH_COMMAND_H__

#include <memory>

namespace afc::editor {
class Command;
class EditorState;
std::unique_ptr<Command> NewSearchCommand(EditorState& editor_state);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_SEARCH_COMMAND_H__
