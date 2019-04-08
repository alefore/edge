#ifndef __AFC_EDITOR_DELETE_MODE_H__
#define __AFC_EDITOR_DELETE_MODE_H__

#include <memory>

#include "command_with_modifiers.h"

namespace afc {
namespace editor {
std::unique_ptr<Transformation> ApplyDeleteCommand(EditorState* editor_state,
                                                   OpenBuffer* buffer,
                                                   Modifiers modifiers);
}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_DELETE_MODE_H__
