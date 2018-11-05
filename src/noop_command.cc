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

std::shared_ptr<Command> NoopCommand() {
  return std::make_shared<NoopCommandImpl>();
}

}  // namespace editor
}  // namespace afc
