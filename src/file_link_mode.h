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

struct OpenFileOptions {
  OpenFileOptions() {}

  EditorState* editor_state = nullptr;
  string name;
  string path;
  bool ignore_if_not_found = false;
  bool make_current_buffer = true;

  // Should the contents of the search paths buffer be used to find the file?
  bool use_search_paths = true;
};

// Creates a new buffer for the file at the path given.
map<string, shared_ptr<OpenBuffer>>::iterator OpenFile(
    const OpenFileOptions& options);

void OpenAnonymousBuffer(EditorState* editor_state);

unique_ptr<EditorMode> NewFileLinkMode(
    EditorState* editor_state, const string& path, bool ignore_if_not_found);

}  // namespace editor
}  // namespace afc

#endif
