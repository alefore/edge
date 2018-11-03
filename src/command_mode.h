#ifndef __AFC_EDITOR_COMMAND_MODE_H__
#define __AFC_EDITOR_COMMAND_MODE_H__

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "editor_mode.h"

namespace afc {
namespace editor {

class EditorState;

using std::unique_ptr;

// parent_mode may be nullptr.
unique_ptr<EditorMode> NewCommandMode(EditorState* editor_state,
                                      std::shared_ptr<EditorMode> parent_mode);

}  // namespace editor
}  // namespace afc

#endif
