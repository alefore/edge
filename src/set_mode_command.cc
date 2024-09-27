#include "src/set_mode_command.h"

#include <glog/logging.h>

#include <map>
#include <memory>

#include "src/editor.h"
#include "src/editor_mode.h"

namespace gc = afc::language::gc;

using afc::infrastructure::ExtendedChar;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::lazy_string::LazyString;

namespace afc::editor {
namespace {
class SetModeCommand : public Command {
 public:
  SetModeCommand(SetModeCommandOptions options)
      : options_(std::move(options)) {}

  CommandCategory Category() const override { return options_.category; }
  LazyString Description() const override { return options_.description; }
  void ProcessInput(ExtendedChar) override {
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
