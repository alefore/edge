#include "src/quit_command.h"

#include <memory>

#include "src/command.h"
#include "src/editor.h"

namespace afc {
namespace editor {

namespace {

class QuitCommand : public Command {
 public:
  QuitCommand(EditorState& editor_state, int exit_value)
      : editor_state_(editor_state), exit_value_(exit_value) {}
  wstring Description() const override {
    return L"Quits Edge (with an exit value of " +
           std::to_wstring(exit_value_) + L").";
  }
  wstring Category() const override { return L"Editor"; }

  void ProcessInput(wint_t) override {
    wstring error_description;
    LOG(INFO) << "Triggering termination with value: " << exit_value_;
    editor_state_.Terminate(
        editor_state_.modifiers().strength <= Modifiers::Strength::kNormal
            ? EditorState::TerminationType::kWhenClean
            : EditorState::TerminationType::kIgnoringErrors,
        exit_value_);
    auto buffer = editor_state_.current_buffer();
    if (buffer != nullptr) {
      buffer->ResetMode();
    }
  }

 private:
  EditorState& editor_state_;
  const int exit_value_;
};

}  // namespace

std::unique_ptr<Command> NewQuitCommand(EditorState& editor_state,
                                        int exit_value) {
  return std::make_unique<QuitCommand>(editor_state, exit_value);
}

}  // namespace editor
}  // namespace afc
