#include "src/quit_command.h"

#include <memory>

#include "src/command.h"
#include "src/editor.h"
#include "src/language/safe_types.h"

namespace gc = afc::language::gc;

using afc::infrastructure::ExtendedChar;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::VisitPointer;
using afc::language::lazy_string::LazyString;

namespace afc::editor {
namespace {
class QuitCommand : public Command {
 public:
  QuitCommand(EditorState& editor_state, int exit_value)
      : editor_state_(editor_state), exit_value_(exit_value) {}
  LazyString Description() const override {
    return LazyString{L"Quits Edge (with an exit value of "} +
           LazyString{std::to_wstring(exit_value_)} + LazyString{L")."};
  }
  CommandCategory Category() const override {
    return CommandCategory::kEditor();
  }

  void ProcessInput(ExtendedChar) override {
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

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const override {
    return {};
  }

 private:
  EditorState& editor_state_;
  const int exit_value_;
};

}  // namespace

language::gc::Root<Command> NewQuitCommand(EditorState& editor_state,
                                           int exit_value) {
  return editor_state.gc_pool().NewRoot(
      MakeNonNullUnique<QuitCommand>(editor_state, exit_value));
}

}  // namespace afc::editor
