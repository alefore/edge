#ifndef __AFC_EDITOR_RUN_COMMAND_HANDLER_H__
#define __AFC_EDITOR_RUN_COMMAND_HANDLER_H__

#include <map>
#include <memory>
#include <string>

#include "src/buffer_name.h"
#include "src/command.h"
#include "src/futures/futures.h"
#include "src/widget_list.h"

namespace afc {
namespace editor {

using std::map;
using std::string;
using std::unique_ptr;
using std::wstring;

class EditorState;

struct ForkCommandOptions {
  static void Register(vm::Environment* environment);

  // The command to run.
  wstring command;

  std::optional<BufferName> name = std::nullopt;

  // Additional environment variables (e.g. getenv) to give to the command.
  map<wstring, wstring> environment = {};

  BuffersList::AddBufferType insertion_type =
      BuffersList::AddBufferType::kVisit;

  // If non-empty, change to this directory in the children. Ignored if empty.
  std::optional<Path> children_path = std::nullopt;
};

unique_ptr<Command> NewForkCommand(EditorState& editor_state);

class OpenBuffer;

std::shared_ptr<OpenBuffer> ForkCommand(EditorState* editor_state,
                                        const ForkCommandOptions& options);

futures::Value<EmptyValue> RunCommandHandler(
    const wstring& input, EditorState* editor_state,
    std::map<wstring, wstring> environment);
futures::Value<EmptyValue> RunMultipleCommandsHandler(
    const wstring& input, EditorState* editor_state);
}  // namespace editor
namespace vm {
template <>
struct VMTypeMapper<editor::ForkCommandOptions*> {
  static editor::ForkCommandOptions* get(Value* value);
  static Value::Ptr New(editor::ForkCommandOptions* value);
  static const VMType vmtype;
};
}  // namespace vm
}  // namespace afc

#endif
