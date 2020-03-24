#ifndef __AFC_EDITOR_FILE_LINK_MODE_H__
#define __AFC_EDITOR_FILE_LINK_MODE_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/editor.h"
#include "src/value_or_error.h"
#include "src/widget_list.h"

namespace afc {
namespace editor {

using std::string;
using std::unique_ptr;

// Saves the contents of the buffer to the path given.
futures::Value<PossibleError> SaveContentsToFile(const wstring& path,
                                                 const BufferContents& contents,
                                                 WorkQueue* work_queue);

struct OpenFileOptions {
  OpenFileOptions() {}

  EditorState* editor_state = nullptr;

  // Name can be empty, in which case the name will come from the path.
  wstring name;

  // The path of the file to open.
  wstring path;
  // TODO: Turn into an enum.
  bool ignore_if_not_found = false;

  BuffersList::AddBufferType insertion_type =
      BuffersList::AddBufferType::kVisit;

  // Should the contents of the search paths buffer be used to find the file?
  bool use_search_paths = true;
  std::vector<std::wstring> initial_search_paths;
};

shared_ptr<OpenBuffer> GetSearchPathsBuffer(EditorState* editor_state);
void GetSearchPaths(EditorState* editor_state, vector<wstring>* output);

// Takes a specification of a path (which can be absolute or relative) and, if
// relative, looks it up in the search paths. If a file is found, returns an
// absolute path pointing to it.
struct ResolvePathOptions {
 public:
  static ResolvePathOptions New(EditorState* editor_state);
  static ResolvePathOptions NewWithEmptySearchPaths(EditorState* editor_state);

  std::wstring path;
  std::vector<std::wstring> search_paths;
  std::wstring home_directory;
  std::function<bool(const wstring&)> validator;

 private:
  ResolvePathOptions() = default;
};

struct ResolvePathOutput {
  // The absolute path pointing to the file. Absent if the operation failed.
  std::wstring path;

  // The position to jump to.
  std::optional<LineColumn> position;

  // The pattern to jump to (after jumping to `position`).
  std::optional<wstring> pattern;
};

std::optional<ResolvePathOutput> ResolvePath(ResolvePathOptions options);

// Creates a new buffer for the file at the path given.
map<wstring, shared_ptr<OpenBuffer>>::iterator OpenFile(
    const OpenFileOptions& options);

shared_ptr<OpenBuffer> OpenAnonymousBuffer(EditorState* editor_state);

}  // namespace editor
}  // namespace afc

#endif
