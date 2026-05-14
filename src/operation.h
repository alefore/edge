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

class CommandArgumentRepetitions {
 public:
  CommandArgumentRepetitions(int repetitions)
      : entries_({{.additive_default = repetitions,
                   .multiplicative_sign = repetitions >= 0 ? 1 : -1}}) {}

  language::lazy_string::SingleLine ToString() const;
  // Returns the total sum of all entries.
  int get() const;
  std::list<int> get_list() const;
  void sum(int value);
  void factor(int value);

  bool empty() const;
  bool PopValue();

 private:
  enum class FactorDirection { kNegative, kPositive };
  struct Entry {
    int additive = 0;
    int additive_default = 0;
    int multiplicative = 0;
    int multiplicative_sign;
  };
  static int Flatten(const Entry& entry);

  std::list<Entry> entries_;
};

// A sequence of arguments becomes a command.
struct CommandReach {
  std::optional<Structure> structure = std::nullopt;
  CommandArgumentRepetitions repetitions = {0};
};

struct CommandReachBegin {
  std::optional<Structure> structure = std::nullopt;
  CommandArgumentRepetitions repetitions = {1};
  Direction direction = Direction::Forwards;
};

// Similar to CommandReach with structure = StructureLine.
//
// We separate them to avoid clashes of 'h' and 'l'. With CommandReach, 'h' and
// 'l' should advance by the structure; with CommandReachLine, they switch us
// back to CommandReach (to move left or right).
struct CommandReachLine {
  CommandArgumentRepetitions repetitions = {0};
};

// Similar to CommandReachLine.
struct CommandReachPage {
  CommandArgumentRepetitions repetitions = {0};
};

// Finds occurrences of a given string.
struct CommandReachQuery {
  language::lazy_string::SingleLine query;
};

struct CommandSetShell {
  language::lazy_string::SingleLine input;
};

struct CommandPaste {
  CommandArgumentRepetitions repetitions = {1};
  std::vector<language::lazy_string::LazyString> queries;
  std::optional<language::lazy_string::SingleLine> query_input;
};

class MoveOperationCommand {
 public:
  virtual ~MoveOperationCommand() = default;
  virtual void AppendStatus(language::text::LineBuilder&) const = 0;
  virtual transformation::Stack GetTransformation(
      const language::NonNull<std::shared_ptr<OperationScope>>& scope,
      transformation::Stack& stack) const = 0;
  virtual KeyCommandsMap key_commands_map() = 0;
};

// TODO(2026-05-14, P2): Convert all to MoveOperationCommand.
using Command =
    std::variant<CommandReach, CommandReachBegin, CommandReachLine,
                 CommandReachPage, CommandReachQuery, CommandSetShell,
                 CommandPaste,
                 language::NonNull<std::shared_ptr<MoveOperationCommand>>>;

language::gc::Root<afc::editor::Command> NewTopLevelCommand(
    std::wstring name, language::lazy_string::LazyString description,
    TopCommand top_command, EditorState& editor_state, Command command);

namespace commands {
// TODO(easy, 2026-05-14): Change this to return a Line instead.
void SerializeCall(language::lazy_string::NonEmptySingleLine name,
                   std::vector<language::lazy_string::SingleLine> arguments,
                   language::text::LineBuilder& output);
language::lazy_string::NonEmptySingleLine StructureToString(
    std::optional<Structure> structure);
}  // namespace commands
}  // namespace operation
}  // namespace afc::editor
#endif  // __AFC_EDITOR_OPERATION_H__
