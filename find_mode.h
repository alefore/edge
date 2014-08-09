#ifndef __AFC_EDITOR_FIND_MODE_H__
#define __AFC_EDITOR_FIND_MODE_H__

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "editor.h"

namespace afc {
namespace editor {

using std::unique_ptr;

unique_ptr<EditorMode> NewFindMode();

}  // namespace editor
}  // namespace afc

#endif
