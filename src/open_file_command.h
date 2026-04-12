#ifndef __AFC_EDITOR_OPEN_FILE_COMMAND_H__
#define __AFC_EDITOR_OPEN_FILE_COMMAND_H__

#include <memory>

#include "src/futures/futures.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/single_line.h"

namespace afc::editor {
class EditorState;
class Command;
language::gc::Root<Command> NewOpenFileCommand(EditorState& editor);

// TODO(P2, trivial, 2026-04-13): Rename to just OpenFileOptions. It currently
// clashes with the one in src/file_link_mode.h.
struct OpenFilesOptions {
  EditorState& editor;

  enum class NotFoundHandler { kIgnore, kCreate };
  NotFoundHandler not_found_handler;

  language::lazy_string::SingleLine path_pattern;
};

// Attempts to open a file. Unlike the lower-level functions in
// src/file_link_mode.h, supports globbing and positions (e.g., `foo.cc:12`).
futures::Value<language::EmptyValue> OpenFiles(OpenFilesOptions);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_OPEN_FILE_COMMAND_H__
