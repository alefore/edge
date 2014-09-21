#ifndef __AFC_EDITOR_SECOND_MODE_H__
#define __AFC_EDITOR_SECOND_MODE_H__

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "editor_mode.h"

namespace afc {
namespace editor {

using std::unique_ptr;

unique_ptr<EditorMode> NewSecondaryMode();

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_SECOND_MODE_H__
