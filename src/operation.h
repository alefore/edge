#ifndef __AFC_EDITOR_OPERATION_H__
#define __AFC_EDITOR_OPERATION_H__

#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "src/command.h"
#include "src/futures/futures.h"
#include "src/modifiers.h"

namespace afc::editor {
class EditorState;
namespace operation {
enum class ApplicationType { kPreview, kCommit };

// erase line to-end reverse
// erase backwards line 4 word 3 char 2)
// Erase line 123 +1 +1 +5 word +5 char +5
// scroll down 5 left 10 +1 +1
// insert "" +5 "alejo"
// navigate-buffer replace-all new-position /foo 5 left space left left
// Reach next-line 5 +1 +1 by-character space 3
// => Reach(Line, 7); ReachNextCharacter(" ", 3);

// Operation that is executed if there are no arguments at all.
struct TopCommandErase {
  Modifiers::DeleteBehavior delete_behavior =
      Modifiers::DeleteBehavior::kDeleteText;
};
struct TopCommandReach {};

using TopCommand = std::variant<TopCommandErase, TopCommandReach>;

class CommandArgumentRepetitions {
 public:
  CommandArgumentRepetitions(int repetitions) {
    entries_.push_back({.additive_default = repetitions,
                        .multiplicative_sign = repetitions >= 0 ? 1 : -1});
  }

  std::wstring ToString() const;
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
struct CommandErase {
  Structure* structure = nullptr;
  CommandArgumentRepetitions repetitions = {.repetitions = 1};
};

struct CommandReach {
  Structure* structure = nullptr;
  CommandArgumentRepetitions repetitions = {.repetitions = 0};
};

struct CommandReachBegin {
  Structure* structure = nullptr;
  CommandArgumentRepetitions repetitions = {.repetitions = 1};
  Direction direction = Direction::kForwards;
};

// Similar to CommandReach with structure = StructureLine.
//
// We separate them to avoid clashes of 'h' and 'l'. With CommandReach, 'h' and
// 'l' should advance by the structure; with CommandReachLine, they switch us
// back to CommandReach (to move left or right).
struct CommandReachLine {
  CommandArgumentRepetitions repetitions = {.repetitions = 0};
};

// Finds occurrences of a given character in the current line.
struct CommandReachChar {
  std::optional<wchar_t> c = std::nullopt;
  CommandArgumentRepetitions repetitions = {.repetitions = 1};
};

using Command = std::variant<CommandErase, CommandReach, CommandReachBegin,
                             CommandReachLine, CommandReachChar>;

using UndoCallback = std::function<futures::Value<EmptyValue>()>;

std::unique_ptr<afc::editor::Command> NewTopLevelCommand(
    std::wstring name, std::wstring description, TopCommand top_command,
    EditorState* editor_state, std::vector<Command> commands);

}  // namespace operation
}  // namespace afc::editor
#endif  // __AFC_EDITOR_OPERATION_H__
