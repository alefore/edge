#ifndef __AFC_EDITOR_RUN_COMMAND_HANDLER_H__
#define __AFC_EDITOR_RUN_COMMAND_HANDLER_H__

#include <map>
#include <memory>
#include <string>

#include "src/buffer_name.h"
#include "src/buffers_list.h"
#include "src/command.h"
#include "src/futures/futures.h"
#include "src/widget_list.h"

namespace afc::language::gc {
class Pool;
}
namespace afc {
namespace editor {

using std::map;
using std::string;
using std::unique_ptr;
using std::wstring;

class EditorState;

struct ForkCommandOptions {
  static void Register(language::gc::Pool& pool, vm::Environment& environment);

  // The command to run.
  wstring command;

  std::optional<BufferName> name = std::nullopt;

  // Additional environment variables (e.g. getenv) to give to the command.
  map<wstring, wstring> environment = {};

  BuffersList::AddBufferType insertion_type =
      BuffersList::AddBufferType::kVisit;

  // If non-empty, change to this directory in the children. Ignored if empty.
  std::optional<infrastructure::Path> children_path = std::nullopt;
};

language::NonNull<std::unique_ptr<Command>> NewForkCommand(
    EditorState& editor_state);

class OpenBuffer;

language::NonNull<std::shared_ptr<OpenBuffer>> ForkCommand(
    EditorState& editor_state, const ForkCommandOptions& options);

futures::Value<language::EmptyValue> RunCommandHandler(
    const wstring& input, EditorState& editor_state,
    std::map<wstring, wstring> environment);
futures::Value<language::EmptyValue> RunMultipleCommandsHandler(
    const wstring& input, EditorState& editor_state);
}  // namespace editor
namespace vm {
template <>
struct VMTypeMapper<editor::ForkCommandOptions*> {
  static editor::ForkCommandOptions* get(Value& value);
  static language::gc::Root<Value> New(language::gc::Pool& pool,
                                       editor::ForkCommandOptions* value);
  static const VMType vmtype;
};
}  // namespace vm
}  // namespace afc

#endif
