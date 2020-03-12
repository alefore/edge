#include "src/delete_mode.h"

#include <memory>

#include "src/command_with_modifiers.h"
#include "src/editor.h"
#include "src/terminal.h"
#include "src/transformation/delete.h"
#include "src/transformation/type.h"

namespace afc {
namespace editor {

std::unique_ptr<Transformation> ApplyDeleteCommand(EditorState* editor_state,
                                                   Modifiers modifiers) {
  CHECK(editor_state != nullptr);
  transformation::Delete options;
  options.modifiers = std::move(modifiers);
  return transformation::Build(options);
}

}  // namespace editor
}  // namespace afc
