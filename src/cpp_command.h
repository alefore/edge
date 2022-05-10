#ifndef __AFC_EDITOR_CPP_COMMAND_H__
#define __AFC_EDITOR_CPP_COMMAND_H__

#include <memory>
#include <string>

#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/language/value_or_error.h"
#include "vm/public/vm.h"

namespace afc::editor {
class EditorState;
class Command;

language::ValueOrError<language::NonNull<std::unique_ptr<Command>>>
NewCppCommand(EditorState& editor_state,
              language::gc::Root<afc::vm::Environment> environment,
              const std::wstring code);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_CPP_COMMAND_H__
