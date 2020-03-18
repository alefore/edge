#ifndef __AFC_EDITOR_DELETE_MODE_H__
#define __AFC_EDITOR_DELETE_MODE_H__

#include <memory>

#include "command_with_modifiers.h"

namespace afc {
namespace editor {
transformation::Variant ApplyDeleteCommand(EditorState* editor_state,
                                           Modifiers modifiers);
}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_DELETE_MODE_H__
