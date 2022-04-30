#ifndef __AFC_EDITOR_FILE_LINK_MODE_H__
#define __AFC_EDITOR_FILE_LINK_MODE_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/editor.h"
#include "src/futures/futures.h"
#include "src/infrastructure/dirname.h"
#include "src/language/value_or_error.h"
#include "src/widget_list.h"

namespace afc {
namespace editor {

using std::string;

// Saves the contents of the buffer to the path given.
futures::Value<language::PossibleError> SaveContentsToFile(
    const infrastructure::Path& path,
    language::NonNull<std::unique_ptr<const BufferContents>> contents,
    concurrent::ThreadPool& thread_pool,
    infrastructure::FileSystemDriver& file_system_driver);

struct OpenFileOptions {
  EditorState& editor_state;

  // Name can be absent, in which case the name will come from the path.
  std::optional<BufferName> name = std::nullopt;

  // The path of the file to open.
  std::optional<infrastructure::Path> path;
  // TODO: Turn into an enum.
  bool ignore_if_not_found = false;

  BuffersList::AddBufferType insertion_type =
      BuffersList::AddBufferType::kVisit;

  // Should the contents of the search paths buffer be used to find the file?
  bool use_search_paths = true;
  std::vector<infrastructure::Path> initial_search_paths = {};
};

futures::Value<std::shared_ptr<OpenBuffer>> GetSearchPathsBuffer(
    EditorState& editor_state);
futures::Value<language::EmptyValue> GetSearchPaths(
    EditorState& editor_state, vector<infrastructure::Path>* output);

// Takes a specification of a path (which can be absolute or relative) and, if
// relative, looks it up in the search paths. If a file is found, returns an
// absolute path pointing to it.
struct ResolvePathOptions {
 public:
  static ResolvePathOptions New(
      EditorState& editor_state,
      std::shared_ptr<infrastructure::FileSystemDriver> file_system_driver);
  static ResolvePathOptions NewWithEmptySearchPaths(
      EditorState& editor_state,
      std::shared_ptr<infrastructure::FileSystemDriver> file_system_driver);

  // This is not a Path because it may contain various embedded tokens such as
  // a ':LINE:COLUMN' suffix. A Path will be extracted from it.
  std::wstring path = L"";
  std::vector<infrastructure::Path> search_paths = {};
  infrastructure::Path home_directory;

  using Validator =
      std::function<futures::Value<bool>(const infrastructure::Path&)>;
  Validator validator = nullptr;

 private:
  ResolvePathOptions(infrastructure::Path home_directory, Validator validator);
  ResolvePathOptions() = default;
};

struct ResolvePathOutput {
  // The absolute path pointing to the file.
  infrastructure::Path path;

  // The position to jump to.
  std::optional<LineColumn> position;

  // The pattern to jump to (after jumping to `position`).
  std::optional<wstring> pattern;
};

futures::ValueOrError<ResolvePathOutput> ResolvePath(
    ResolvePathOptions options);

// Creates a new buffer for the file at the path given.
//
// If `ignore_if_not_found` is true, can return nullptr. Otherwise, will always
// return a value.
futures::Value<std::shared_ptr<OpenBuffer>> OpenFile(
    const OpenFileOptions& options);

futures::Value<language::NonNull<std::shared_ptr<OpenBuffer>>>
OpenAnonymousBuffer(EditorState& editor_state);

}  // namespace editor
}  // namespace afc

#endif
