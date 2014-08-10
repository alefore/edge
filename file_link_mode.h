#ifndef __AFC_EDITOR_FILE_LINK_MODE_H__
#define __AFC_EDITOR_FILE_LINK_MODE_H__

#include <memory>
#include <string>

#include "editor.h"

namespace afc {
namespace editor {

using std::unique_ptr;
using std::string;

unique_ptr<EditorMode> NewFileLinkMode(const string& path, int position);

}  // namespace editor
}  // namespace afc

#endif
