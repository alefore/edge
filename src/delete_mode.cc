#include "src/delete_mode.h"

#include <memory>

#include "src/command_with_modifiers.h"
#include "src/editor.h"
#include "src/terminal.h"
#include "src/transformation/delete.h"

namespace afc {
namespace editor {

std::unique_ptr<Transformation> ApplyDeleteCommand(EditorState* editor_state,
                                                   Modifiers modifiers) {
  CHECK(editor_state != nullptr);
  DeleteOptions options;
  options.modifiers = std::move(modifiers);
  return NewDeleteTransformation(options);
}

}  // namespace editor
}  // namespace afc
