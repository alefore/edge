#ifndef __AFC_EDITOR_RUN_COMMAND_HANDLER_H__
#define __AFC_EDITOR_RUN_COMMAND_HANDLER_H__

#include <map>
#include <memory>
#include <string>

#include "src/buffer_name.h"
#include "src/buffers_list.h"
#include "src/command.h"
#include "src/futures/futures.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/widget_list.h"

namespace afc::language::gc {
class Pool;
}
namespace afc::vm {
class CppString;
}
namespace afc::editor {
class EditorState;

language::gc::Root<Command> NewRunCommandCommand(EditorState& editor_state);

class OpenBuffer;

futures::Value<language::EmptyValue> RunMultipleCommandsHandler(
    EditorState& editor_state, language::lazy_string::SingleLine input);
}  // namespace afc::editor

#endif
