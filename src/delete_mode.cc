#include "delete_mode.h"

#include <memory>

#include "command_with_modifiers.h"
#include "editor.h"
#include "terminal.h"
#include "transformation_delete.h"

namespace afc {
namespace editor {

void ApplyDeleteCommand(EditorState* editor_state, OpenBuffer* buffer,
                        CommandApplyMode apply_mode, Modifiers modifiers) {
  CHECK(editor_state != nullptr);
  CHECK(buffer != nullptr);
  DeleteOptions options;
  options.modifiers = modifiers;
  options.copy_to_paste_buffer = apply_mode == CommandApplyMode::FINAL;
  options.preview = apply_mode == CommandApplyMode::PREVIEW;

  buffer->PushTransformationStack();
  buffer->ApplyToCursors(NewDeleteTransformation(options),
                         modifiers.cursors_affected);
  buffer->PopTransformationStack();
}

}  // namespace editor
}  // namespace afc
