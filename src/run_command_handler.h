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

struct ForkCommandOptions {
  static void Register(language::gc::Pool& pool, vm::Environment& environment);

  // The command to run.
  std::wstring command;

  std::optional<BufferName> name = std::nullopt;

  // Additional environment variables (e.g. getenv) to give to the command.
  std::map<std::wstring, language::lazy_string::LazyString> environment = {};

  BuffersList::AddBufferType insertion_type =
      BuffersList::AddBufferType::kVisit;

  // If non-empty, change to this directory in the children. Ignored if empty.
  std::optional<infrastructure::Path> children_path = std::nullopt;

  // What should we do if the buffer already existed?
  enum class ExistingBufferBehavior {
    // Reuse it (initiates a reload).
    kReuse,
    // Ignore the previous buffer. Create a new one.
    kIgnore
  };
  ExistingBufferBehavior existing_buffer_behavior =
      ExistingBufferBehavior::kReuse;
};

language::gc::Root<Command> NewForkCommand(EditorState& editor_state);

class OpenBuffer;

language::gc::Root<OpenBuffer> ForkCommand(EditorState& editor_state,
                                           const ForkCommandOptions& options);

futures::Value<language::EmptyValue> RunMultipleCommandsHandler(
    EditorState& editor_state, language::lazy_string::LazyString input);
}  // namespace afc::editor

#endif
