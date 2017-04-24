#include "delete_mode.h"

#include <memory>

#include "editor.h"
#include "terminal.h"
#include "command_with_modifiers.h"
#include "transformation_delete.h"

namespace afc {
namespace editor {

void ApplyDeleteCommand(EditorState* editor_state, OpenBuffer* buffer,
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

}  // namespace afc
}  // namespace editor
