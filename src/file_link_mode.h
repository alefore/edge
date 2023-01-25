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

  BuffersList::AddBufferType insertion_type =
      BuffersList::AddBufferType::kVisit;

  // Should the contents of the search paths buffer be used to find the file?
  bool use_search_paths = true;
  std::vector<infrastructure::Path> initial_search_paths = {};
};

futures::Value<language::EmptyValue> GetSearchPaths(
    EditorState& editor_state, std::vector<infrastructure::Path>* output);

// Takes a specification of a path (which can be absolute or relative) and, if
// relative, looks it up in the search paths. If a file is found, returns an
// absolute path pointing to it.
template <typename ValidatorOutput>
struct ResolvePathOptions {
 public:
  static ResolvePathOptions New(
      EditorState& editor_state,
      std::shared_ptr<infrastructure::FileSystemDriver> file_system_driver) {
    auto output =
        NewWithEmptySearchPaths(editor_state, std::move(file_system_driver));
    GetSearchPaths(editor_state, &output.search_paths);
    return output;
  }

  static ResolvePathOptions NewWithEmptySearchPaths(
      EditorState& editor_state,
      std::shared_ptr<infrastructure::FileSystemDriver> file_system_driver) {
    return ResolvePathOptions(
        editor_state.home_directory(),
        [file_system_driver](const infrastructure::Path& path) {
          return CanStatPath(file_system_driver, path);
        });
  }

  // This is not a Path because it may contain various embedded tokens such as
  // a ':LINE:COLUMN' suffix. A Path will be extracted from it.
  std::wstring path = L"";
  std::vector<infrastructure::Path> search_paths = {};
  infrastructure::Path home_directory;

  using Validator = std::function<futures::ValueOrError<ValidatorOutput>(
      const infrastructure::Path&)>;
  Validator validator = nullptr;

  static futures::Value<language::PossibleError> CanStatPath(
      std::shared_ptr<infrastructure::FileSystemDriver> file_system_driver,
      const infrastructure::Path& path) {
    VLOG(5) << "Considering path: " << path;
    return file_system_driver->Stat(path).Transform(
        [](struct stat) { return language::Success(); });
  }

 private:
  ResolvePathOptions(infrastructure::Path input_home_directory,
                     Validator input_validator)
      : home_directory(std::move(input_home_directory)),
        validator(std::move(input_validator)) {}

  ResolvePathOptions() = default;
};

template <typename ValidatorOutput>
struct ResolvePathOutput {
  // The absolute path pointing to the file.
  infrastructure::Path path;

  // The position to jump to.
  std::optional<LineColumn> position;

  // The pattern to jump to (after jumping to `position`).
  std::optional<std::wstring> pattern;

  ValidatorOutput validator_output;
};

futures::ValueOrError<ResolvePathOutput<language::EmptyValue>> ResolvePath(
    ResolvePathOptions<language::EmptyValue> options);

futures::ValueOrError<language::gc::Root<OpenBuffer>> OpenFileIfFound(
    const OpenFileOptions& options);

futures::Value<language::gc::Root<OpenBuffer>> OpenOrCreateFile(
    const OpenFileOptions& options);

futures::Value<language::gc::Root<OpenBuffer>> OpenAnonymousBuffer(
    EditorState& editor_state);

}  // namespace editor
}  // namespace afc

#endif
