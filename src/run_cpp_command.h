#ifndef __AFC_EDITOR_RUN_CPP_COMMAND_H__
#define __AFC_EDITOR_RUN_CPP_COMMAND_H__

#include <memory>

#include "command.h"

namespace afc {
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

std::unique_ptr<Command> NewRunCppCommand(CppCommandMode mode);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_RUN_CPP_COMMAND_H__
