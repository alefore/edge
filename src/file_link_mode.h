#ifndef __AFC_EDITOR_FILE_LINK_MODE_H__
#define __AFC_EDITOR_FILE_LINK_MODE_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/editor.h"
#include "src/futures/futures.h"
#include "src/infrastructure/dirname.h"
#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/text/line_sequence.h"
#include "src/widget_list.h"

namespace afc {
namespace editor {
// Saves the contents of the buffer to the path given.
futures::Value<language::PossibleError> SaveContentsToFile(
    const infrastructure::Path& path, language::text::LineSequence contents,
    concurrent::ThreadPoolWithWorkQueue& thread_pool,
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

  // You can use this if you want to ignore specific files.
  std::function<language::PossibleError(struct stat)> stat_validator =
      [](struct stat) { return language::Success(); };
};

futures::Value<std::vector<infrastructure::Path>> GetSearchPaths(
    EditorState& editor_state);

// Takes a specification of a path (which can be absolute or relative) and, if
// relative, looks it up in the search paths. If a file is found, returns an
// absolute path pointing to it.
struct ResolvePathOptions {
 public:
  using ValidatorOutput = std::optional<language::gc::Root<OpenBuffer>>;
  using Validator = std::function<futures::ValueOrError<ValidatorOutput>(
      const infrastructure::Path&)>;

  // This is not a Path because it may contain various embedded tokens such as
  // a ':LINE:COLUMN' suffix. A Path will be extracted from it.
  language::lazy_string::LazyString path = {};
  std::vector<infrastructure::Path> search_paths = {};
  infrastructure::Path home_directory;

  Validator validator = nullptr;

  static futures::Value<ResolvePathOptions> New(
      EditorState& editor_state,
      language::NonNull<std::shared_ptr<infrastructure::FileSystemDriver>>
          file_system_driver) {
    return GetSearchPaths(editor_state)
        .Transform(std::bind_front(&NewWithSearchPaths, std::ref(editor_state),
                                   file_system_driver));
  }

  static ResolvePathOptions NewWithSearchPaths(
      EditorState& editor_state,
      language::NonNull<std::shared_ptr<infrastructure::FileSystemDriver>>
          file_system_driver,
      std::vector<infrastructure::Path> search_paths = {});
};

struct ResolvePathOutput {
  struct Entry {
    // The absolute path pointing to the file.
    infrastructure::Path path;

    // The position to jump to.
    std::optional<language::text::LineColumn> position;

    // The pattern to jump to (after jumping to `position`).
    language::lazy_string::SingleLine pattern;

    ResolvePathOptions::ValidatorOutput validator_output;
  };

  std::vector<Entry> entries;
};

futures::Value<ResolvePathOutput> ResolvePath(ResolvePathOptions input);

futures::ValueOrError<std::vector<language::gc::Root<OpenBuffer>>>
OpenFileIfFound(const OpenFileOptions& options);

futures::Value<std::vector<language::gc::Root<OpenBuffer>>> OpenOrCreateFile(
    const OpenFileOptions& options);

futures::Value<language::gc::Root<OpenBuffer>> OpenAnonymousBuffer(
    EditorState& editor_state);

}  // namespace editor
}  // namespace afc

#endif
