#ifndef __AFC_EDITOR_FILE_LINK_MODE_H__
#define __AFC_EDITOR_FILE_LINK_MODE_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/dirname.h"
#include "src/editor.h"
#include "src/futures/futures.h"
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
  EditorState* editor_state = nullptr;

  // Name can be empty, in which case the name will come from the path.
  wstring name;

  // The path of the file to open.
  std::optional<Path> path;
  // TODO: Turn into an enum.
  bool ignore_if_not_found = false;

  BuffersList::AddBufferType insertion_type =
      BuffersList::AddBufferType::kVisit;

  // Should the contents of the search paths buffer be used to find the file?
  bool use_search_paths = true;
  std::vector<Path> initial_search_paths;
};

futures::Value<std::shared_ptr<OpenBuffer>> GetSearchPathsBuffer(
    EditorState* editor_state);
futures::Value<EmptyValue> GetSearchPaths(EditorState* editor_state,
                                          vector<Path>* output);

// Takes a specification of a path (which can be absolute or relative) and, if
// relative, looks it up in the search paths. If a file is found, returns an
// absolute path pointing to it.
struct ResolvePathOptions {
 public:
  static ResolvePathOptions New(EditorState* editor_state);
  static ResolvePathOptions NewWithEmptySearchPaths(EditorState* editor_state);

  // TODO(easy): Change to type Path.
  std::wstring path = L"";
  std::vector<Path> search_paths = {};
  Path home_directory;

  std::function<futures::Value<bool>(const wstring&)> validator = nullptr;

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

futures::ValueOrError<ResolvePathOutput> ResolvePath(
    ResolvePathOptions options);

// Creates a new buffer for the file at the path given.
futures::Value<map<wstring, shared_ptr<OpenBuffer>>::iterator> OpenFile(
    const OpenFileOptions& options);

futures::Value<shared_ptr<OpenBuffer>> OpenAnonymousBuffer(
    EditorState* editor_state);

}  // namespace editor
}  // namespace afc

#endif
