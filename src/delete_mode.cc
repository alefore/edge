#include "src/delete_mode.h"

#include <memory>

#include "src/command_with_modifiers.h"
#include "src/editor.h"
#include "src/terminal.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"

namespace afc {
namespace editor {

// TODO(easy): Just kill this? Inline it?
transformation::Variant ApplyDeleteCommand(EditorState*, Modifiers modifiers) {
  transformation::Delete options;
  options.modifiers = std::move(modifiers);
  return options;
}

}  // namespace editor
}  // namespace afc
