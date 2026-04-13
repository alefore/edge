#ifndef __AFC_EDITOR_OPEN_FILE_COMMAND_H__
#define __AFC_EDITOR_OPEN_FILE_COMMAND_H__

#include <memory>

#include "src/futures/futures.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/single_line.h"
#include "src/open_file_position.h"

namespace afc::editor {
class EditorState;
class OpenBuffer;
class Command;
language::gc::Root<Command> NewOpenFileCommand(EditorState& editor);

// TODO(P2, trivial, 2026-04-13): Move to its own module.
struct OpenFilesOptions {
  EditorState& editor;

  enum class NotFoundHandler { kIgnore, kCreate };
  NotFoundHandler not_found_handler;

  language::lazy_string::SingleLine path_pattern;

  open_file_position::SuffixMode open_file_position_suffix_mode =
      open_file_position::SuffixMode::Disallow;
};

// Attempts to open a file. Unlike the lower-level functions in
// src/file_link_mode.h, supports globbing and positions (e.g., `foo.cc:12`).
futures::Value<std::vector<language::gc::Root<OpenBuffer>>> OpenFiles(
    OpenFilesOptions);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_OPEN_FILE_COMMAND_H__
