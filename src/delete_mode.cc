#include "delete_mode.h"

#include <memory>

#include "editor.h"
#include "terminal.h"
#include "command_with_modifiers.h"
#include "transformation_delete.h"

namespace afc {
namespace editor {

namespace {
void ApplyDelete(EditorState* editor_state, OpenBuffer* buffer,
                 CommandApplyMode apply_mode, Modifiers modifiers) {
  CHECK(editor_state != nullptr);
  CHECK(buffer != nullptr);
  DeleteOptions options;
  options.modifiers = modifiers;
  options.copy_to_paste_buffer = apply_mode == APPLY_FINAL;
  options.preview = apply_mode == APPLY_PREVIEW;

  buffer->PushTransformationStack();
  buffer->ApplyToCursors(NewDeleteTransformation(options));
  buffer->PopTransformationStack();
}
}  // namespace

std::unique_ptr<Command> NewDeleteCommand() {
  return NewCommandWithModifiers(L"delete", L"starts a new delete command",
                                 ApplyDelete);
}

}  // namespace afc
}  // namespace editor
