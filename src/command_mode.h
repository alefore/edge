#ifndef __AFC_EDITOR_COMMAND_MODE_H__
#define __AFC_EDITOR_COMMAND_MODE_H__

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "editor_mode.h"

namespace afc {
namespace editor {

class EditorState;

using std::unique_ptr;

unique_ptr<EditorMode> NewCommandMode(EditorState*);

}  // namespace editor
}  // namespace afc

#endif
