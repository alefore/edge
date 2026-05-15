#ifndef __AFC_EDITOR_OPERATION_H__
#define __AFC_EDITOR_OPERATION_H__

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "src/command.h"
#include "src/futures/futures.h"
#include "src/key_commands_map.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/modifiers.h"
#include "src/operation_repetitions.h"
#include "src/transformation_stack.h"

namespace afc::editor {
class EditorState;
namespace operation {
enum class ApplicationType { Preview, Commit };

struct TopCommand {
  transformation::Stack::PostTransformationBehavior
      post_transformation_behavior;
  bool show_help = false;
};

struct CommandReachBegin {
  Structure structure = Structure::Char;
  commands::Repetitions repetitions = {1};
  Direction direction = Direction::Forwards;
};

struct CommandPaste {
  commands::Repetitions repetitions = {1};
  std::vector<language::lazy_string::LazyString> queries;
  std::optional<language::lazy_string::SingleLine> query_input;
};

class MoveOperationCommand {
 public:
  virtual ~MoveOperationCommand() = default;
  virtual language::text::LineBuilder status() const = 0;
  virtual transformation::Stack GetTransformation(
      const language::NonNull<std::shared_ptr<OperationScope>>& scope,
      transformation::Stack& stack) const = 0;

  // `push_command` can be invoked by a key command in order to push a new
  // MoveOperationCommand into the stack.
  using Receiver = std::function<void(
      language::NonNull<std::shared_ptr<MoveOperationCommand>>)>;
  virtual KeyCommandsMap key_commands_map(Receiver push_command) = 0;
  virtual commands::Repetitions* repetitions() = 0;
};

// TODO(2026-05-14, P2): Convert all to MoveOperationCommand.
using Command =
    std::variant<CommandReachBegin, CommandPaste,
                 language::NonNull<std::shared_ptr<MoveOperationCommand>>>;

language::gc::Root<afc::editor::Command> NewTopLevelCommand(
    std::wstring name, language::lazy_string::LazyString description,
    TopCommand top_command, EditorState& editor_state,
    std::function<Command()> command_factory);

namespace commands {
// Functions for implementations of MoveOperationCommand.

// TODO(easy, 2026-05-14): Change this to return a LineBuilder instead.
void SerializeCall(language::lazy_string::NonEmptySingleLine name,
                   std::vector<language::lazy_string::SingleLine> arguments,
                   language::text::LineBuilder& output);

void AddSetStructureChar(KeyCommandsMap& cmap, Structure& structure,
                         Repetitions& repetitions);

language::lazy_string::NonEmptySingleLine StructureToString(
    std::optional<Structure> structure);
}  // namespace commands
}  // namespace operation
}  // namespace afc::editor
#endif  // __AFC_EDITOR_OPERATION_H__
