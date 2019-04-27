#include "src/quit_command.h"

#include <memory>

#include "src/command.h"
#include "src/editor.h"

namespace afc {
namespace editor {

namespace {

class QuitCommand : public Command {
 public:
  QuitCommand(int exit_value) : exit_value_(exit_value) {}
  wstring Description() const override {
    return L"Quits Edge (with an exit value of " +
           std::to_wstring(exit_value_) + L").";
  }
  wstring Category() const override { return L"Editor"; }

  void ProcessInput(wint_t, EditorState* editor_state) override {
    wstring error_description;
    editor_state->AttemptTermination(exit_value_);
    auto buffer = editor_state->current_buffer();
    if (buffer != nullptr) {
      buffer->ResetMode();
    }
  }

 private:
  const int exit_value_;
};

}  // namespace

std::unique_ptr<Command> NewQuitCommand(int exit_value) {
  return std::make_unique<QuitCommand>(exit_value);
}

}  // namespace editor
}  // namespace afc
