#ifndef __AFC_EDITOR_SEARCH_MODE_H__
#define __AFC_EDITOR_SEARCH_MODE_H__

#include <memory>

#include "editor.h"

namespace afc {
namespace editor {

using std::unique_ptr;

unique_ptr<EditorMode> NewSearchMode();

}  // namespace editor
}  // namespace afc

#endif
