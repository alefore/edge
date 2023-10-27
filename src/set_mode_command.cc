#include "src/set_mode_command.h"

#include <glog/logging.h>

#include <map>
#include <memory>

#include "src/editor.h"
#include "src/editor_mode.h"

namespace gc = afc::language::gc;

namespace afc::editor {
using language::MakeNonNullUnique;
using language::NonNull;
namespace {
class SetModeCommand : public Command {
 public:
  SetModeCommand(SetModeCommandOptions options)
      : options_(std::move(options)) {}

  std::wstring Category() const override { return options_.category; }
  std::wstring Description() const override { return options_.description; }
  void ProcessInput(wint_t) override {
    options_.editor_state.set_keyboard_redirect(options_.factory());
  }

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const override {
    return {};
  }

 private:
  const SetModeCommandOptions options_;
};
}  // namespace

gc::Root<Command> NewSetModeCommand(SetModeCommandOptions options) {
  return options.editor_state.gc_pool().NewRoot(
      MakeNonNullUnique<SetModeCommand>(std::move(options)));
}

}  // namespace afc::editor
