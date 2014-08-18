#ifndef __AFC_EDITOR_FILE_LINK_MODE_H__
#define __AFC_EDITOR_FILE_LINK_MODE_H__

#include <memory>
#include <string>

#include "editor.h"

namespace afc {
namespace editor {

using std::unique_ptr;
using std::string;

// Saves the contents of the buffer to the path given.  If there's an error,
// updates the editor status and returns false; otherwise, returns true (and
// leaves the status unmodified).
bool SaveContentsToFile(
    EditorState* editor_state, OpenBuffer* buffer, const string& path);

// Saves the contents of the buffer directly to an already open file.  Like
// SaveContentsToFile, either returns true (on success) or updates the editor
// status.
bool SaveContentsToOpenFile(
    EditorState* editor_state, OpenBuffer* buffer, const string& path,
    int fd);

unique_ptr<EditorMode> NewFileLinkMode(
    EditorState* editor_state, const string& path, int position,
    bool ignore_if_not_found);

}  // namespace editor
}  // namespace afc

#endif
