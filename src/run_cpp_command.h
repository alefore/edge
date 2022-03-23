#ifndef __AFC_EDITOR_RUN_CPP_COMMAND_H__
#define __AFC_EDITOR_RUN_CPP_COMMAND_H__

#include <memory>

#include "src/command.h"
#include "src/futures/futures.h"

namespace afc {
namespace vm {
class Value;
}
namespace editor {

enum class CppCommandMode {
  // Compiles the string and runs it.
  kLiteral,

  // Splits the string into a vector of strings (respecting quotes). Looks up a
  // C++ (VM) function named after the first token that receives strings and
  // runs it, providing the tokens given.
  //
  // This has nothing to do with the system shell (i.e., system(3)).
  kShell
};

// A command looks like this: build foo "bar hey".
//
// In this case, that'd run something like: build("foo", "bar hey");
futures::Value<std::unique_ptr<vm::Value>> RunCppCommandShell(
    const std::wstring& command, EditorState* editor_state);

std::unique_ptr<Command> NewRunCppCommand(EditorState& editor_state,
                                          CppCommandMode mode);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_RUN_CPP_COMMAND_H__
