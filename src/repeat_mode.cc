#include "src/repeat_mode.h"

#include <memory>

#include "src/command_mode.h"
#include "src/editor.h"
#include "src/infrastructure/extended_char.h"
#include "src/language/overload.h"

namespace gc = afc::language::gc;

using afc::infrastructure::ControlChar;
using afc::infrastructure::ExtendedChar;
using afc::language::MakeNonNullUnique;
using afc::language::overload;

namespace afc::editor {
namespace {
class RepeatMode : public EditorMode {
 public:
  RepeatMode(EditorState& editor_state, std::function<void(int)> consumer)
      : editor_state_(editor_state), consumer_(consumer), result_(0) {}

  void ProcessInput(ExtendedChar input) {
    if (std::optional<size_t> value = std::visit(
            overload{[](wchar_t c) {
                       return c >= '0' && c <= '9' ? c - '0'
                                                   : std::optional<size_t>();
                     },
                     [](ControlChar) { return std::optional<size_t>(); }},
            input);
        value.has_value()) {
      result_ = 10 * result_ + *value;
      consumer_(result_);
    } else {
      consumer_(result_);
      // TODO(trivial, 2024-07-30): Allow the old redirect to be deleted; just
      // make sure we use a copy of editor_state_ instead.
      auto old_mode_to_keep_this_alive =
          editor_state_.set_keyboard_redirect(std::nullopt);
      editor_state_.ProcessInput({input});
    }
  }

  CursorMode cursor_mode() const { return CursorMode::kDefault; }

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const override {
    return {};
  }

 private:
  EditorState& editor_state_;
  const std::function<void(int)> consumer_;
  int result_;
};
}  // namespace

gc::Root<InputReceiver> NewRepeatMode(EditorState& editor_state,
                                      std::function<void(int)> consumer) {
  return editor_state.gc_pool().NewRoot<InputReceiver>(
      MakeNonNullUnique<RepeatMode>(editor_state, std::move(consumer)));
}
}  // namespace afc::editor
