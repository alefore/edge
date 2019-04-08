#include "delete_mode.h"

#include <memory>

#include "command_with_modifiers.h"
#include "editor.h"
#include "terminal.h"
#include "transformation_delete.h"

namespace afc {
namespace editor {

std::unique_ptr<Transformation> ApplyDeleteCommand(EditorState* editor_state,
                                                   OpenBuffer* buffer,
                                                   Modifiers modifiers) {
  CHECK(editor_state != nullptr);
  CHECK(buffer != nullptr);
  DeleteOptions options;
  options.modifiers = modifiers;
  options.copy_to_paste_buffer = true;

  return NewDeleteTransformation(options);
}

}  // namespace editor
}  // namespace afc
