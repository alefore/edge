#ifndef __AFC_EDITOR_REPEAT_MODE_H__
#define __AFC_EDITOR_REPEAT_MODE_H__

#include <functional>
#include <memory>

#include "editor.h"

namespace afc {
namespace editor {

using std::function;
using std::unique_ptr;

unique_ptr<EditorMode> NewRepeatMode(function<void(EditorState*, int)> consumer);

}  // namespace editor
}  // namespace afc

#endif
