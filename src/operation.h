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
enum class TopCommand { kErase, kReach };

class CommandArgumentRepetitions {
 public:
  CommandArgumentRepetitions(int repetitions)
      : additive_default_(repetitions),
        multiplicative_sign_(repetitions >= 0 ? 1 : -1) {}

  std::wstring ToString() const;
  int get() const;
  void sum(int value);
  void factor(int value);

 private:
  // The total will be the sum of these 3 factors.
  int additive_default_ = 0;
  int additive_ = 0;
  int multiplicative_ = 0;
  // Holds the sign of the last value given to `sum`. Used to know if values
  // given to `factor` should be considered positive or negative (i.e., move in
  // the direction that was last given explicitly through `sum`).
  int multiplicative_sign_ = 1;
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

struct CommandReachLine {
  CommandArgumentRepetitions repetitions = {.repetitions = 0};
};

struct CommandReachChar {
  std::optional<wchar_t> c;
  CommandArgumentRepetitions repetitions = {.repetitions = 1};
};

using Command = std::variant<CommandErase, CommandReach, CommandReachBegin,
                             CommandReachLine, CommandReachChar>;

using UndoCallback = std::function<futures::Value<EmptyValue>()>;

std::unique_ptr<afc::editor::Command> NewTopLevelCommand(
    std::wstring name, std::wstring description, TopCommand top_command,
    EditorState* editor_state);

}  // namespace operation
}  // namespace afc::editor
#endif  // __AFC_EDITOR_OPERATION_H__
