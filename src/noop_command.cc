#include "noop_command.h"

#include "command.h"
#include "editor.h"

namespace {

using namespace afc::editor;

class NoopCommandImpl : public Command {
  const wstring Description() { return L"does nothing"; }
  void ProcessInput(wint_t, EditorState*) {}
};

}  // namespace

namespace afc {
namespace editor {

Command* NoopCommand() {
  static auto output = new NoopCommandImpl();
  return output;
}

}  // namespace editor
}  // namespace afc
