#include "src/set_mode_command.h"

#include <glog/logging.h>

#include <map>
#include <memory>

#include "src/editor.h"
#include "src/editor_mode.h"

namespace afc::editor {
namespace {
class SetModeCommand : public Command {
 public:
  SetModeCommand(SetModeCommandOptions options)
      : options_(std::move(options)) {}

  std::wstring Category() const override { return options_.category; }
  std::wstring Description() const override { return options_.description; }
  void ProcessInput(wint_t, EditorState* editor) override {
    editor->set_keyboard_redirect(options_.factory());
  }

 private:
  const SetModeCommandOptions options_;
};
}  // namespace

std::unique_ptr<Command> NewSetModeCommand(SetModeCommandOptions options) {
  return std::make_unique<SetModeCommand>(std::move(options));
}

}  // namespace afc::editor
