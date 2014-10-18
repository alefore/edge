#ifndef __AFC_EDITOR_INSERT_MODE_H__
#define __AFC_EDITOR_INSERT_MODE_H__

#include <memory>

#include "editor.h"

namespace afc {
namespace editor {

using std::unique_ptr;

void EnterInsertMode(EditorState* editor_state);

}  // namespace editor
}  // namespace afc

#endif
