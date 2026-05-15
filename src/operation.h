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

// Wrapper around std::optional<Value> but allowing us to provide a default
// value (and track whether the explicit value has been set).
template <typename Value>
class ValueWithDefault {
  Value default_value_;
  std::optional<Value> value_ = std::nullopt;

 public:
  ValueWithDefault(Value default_value)
      : default_value_(std::move(default_value)) {}
  bool IsExplicit() const { return value_.has_value(); }
  Value value() const { return value_.value_or(default_value_); }
  void Set(Value value) { value_ = value; }
  bool operator==(const Value& v) const { return value() == v; }
};

class MoveOperationCommand {
 public:
  virtual ~MoveOperationCommand() = default;
  virtual language::text::LineBuilder status() const = 0;
  virtual transformation::Variant GetTransformation(
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
    std::variant<language::NonNull<std::shared_ptr<MoveOperationCommand>>>;

language::gc::Root<afc::editor::Command> NewTopLevelCommand(
    std::wstring name, language::lazy_string::LazyString description,
    TopCommand top_command, EditorState& editor_state,
    std::function<Command()> command_factory);

namespace commands {
// Functions for implementations of MoveOperationCommand.

void SerializeCall(language::lazy_string::NonEmptySingleLine name,
                   std::vector<language::lazy_string::SingleLine> arguments,
                   language::text::LineBuilder& output);

void AddSetStructureChar(KeyCommandsMap& cmap,
                         ValueWithDefault<Structure>& structure,
                         Repetitions& repetitions);

language::lazy_string::NonEmptySingleLine StructureToString(
    std::optional<Structure> structure);
}  // namespace commands
}  // namespace operation
}  // namespace afc::editor
#endif  // __AFC_EDITOR_OPERATION_H__
