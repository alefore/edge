#include "src/quit_command.h"

#include <memory>

#include "src/command.h"
#include "src/editor.h"
#include "src/language/safe_types.h"

namespace afc::editor {
using language::MakeNonNullUnique;
using language::NonNull;
using language::VisitPointer;

namespace gc = language::gc;

namespace {
class QuitCommand : public Command {
 public:
  QuitCommand(EditorState& editor_state, int exit_value)
      : editor_state_(editor_state), exit_value_(exit_value) {}
  std::wstring Description() const override {
    return L"Quits Edge (with an exit value of " +
           std::to_wstring(exit_value_) + L").";
  }
  std::wstring Category() const override { return L"Editor"; }

  void ProcessInput(wint_t) override {
    std::wstring error_description;
    LOG(INFO) << "Triggering termination with value: " << exit_value_;
    editor_state_.Terminate(
        editor_state_.modifiers().strength <= Modifiers::Strength::kNormal
            ? EditorState::TerminationType::kWhenClean
            : EditorState::TerminationType::kIgnoringErrors,
        exit_value_);
    VisitPointer(
        editor_state_.current_buffer(),
        [](gc::Root<OpenBuffer> buffer) { buffer.ptr()->ResetMode(); }, [] {});
  }

 private:
  EditorState& editor_state_;
  const int exit_value_;
};

}  // namespace

NonNull<std::unique_ptr<Command>> NewQuitCommand(EditorState& editor_state,
                                                 int exit_value) {
  return MakeNonNullUnique<QuitCommand>(editor_state, exit_value);
}

}  // namespace afc::editor
