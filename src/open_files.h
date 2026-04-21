#ifndef __AFC_EDITOR_OPEN_FILES_H__
#define __AFC_EDITOR_OPEN_FILES_H__

#include <memory>

#include "src/buffers_list.h"
#include "src/file_predictor.h"
#include "src/futures/futures.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/single_line.h"
#include "src/open_file_position.h"

namespace afc::editor {
class OpenBuffer;
struct OpenFilesOptions {
  EditorState& editor;

  // Allows an `OpenFiles` operation to stop before the entirety of matches
  // (from path_pattern) have been found. We may open more files than the limit
  // (this limit is passed to `FilePredictor`, but as many calls as there are
  // search paths are made).
  //
  // If this is set, the subset returned is somewhat random (depends on the
  // order of files in directories, not alphabetic order).
  //
  // This is mostly useful meant as an optimization for operations that know
  // that they'll use at most a single file.
  std::optional<size_t> match_limit = std::nullopt;

  enum class NotFoundHandler { kIgnore, kCreate };
  NotFoundHandler not_found_handler;

  // TODO(P1, tricky, 2026-04-13): There's a mismatch here. We'd like to make
  // this a LazyString (rather than SingleLine), to support opening files with
  // \n in their names. However, the predictors take SingleLine elements (and it
  // might be annoying to change all of them to support multi-line inputs, since
  // the outputs, which are appended to a buffer, depend on the inputs).
  language::lazy_string::SingleLine path_pattern;

  open_file_position::SuffixMode open_file_position_suffix_mode =
      open_file_position::SuffixMode::Disallow;

  BuffersList::AddBufferType insertion_type =
      BuffersList::AddBufferType::kVisit;

  FilePredictorOptions::Filter directory_filter =
      FilePredictorOptions::Filter::Include;
  FilePredictorOptions::Filter special_file_filter =
      FilePredictorOptions::Filter::Include;
};

// Attempts to open a file. Unlike the lower-level functions in
// src/file_link_mode.h, supports globbing and positions (e.g., `foo.cc:12`).
futures::Value<std::vector<language::gc::Root<OpenBuffer>>> OpenFiles(
    OpenFilesOptions);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_OPEN_FILES_H__
