#ifndef __AFC_EDITOR_LOG_CONFIG_LOADER_H__
#define __AFC_EDITOR_LOG_CONFIG_LOADER_H__

#include <expected>

#include "src/editor.h"
#include "src/futures/futures.h"
#include "src/language/error/value_or_error.h"
#include "src/language/text/line_sequence.h"
#include "src/log_model.h"

namespace afc::editor {
std::expected<LogModel, language::Error> ParseLogConfig(
    const language::text::LineSequence& lines);

futures::ValueOrError<LogModel> LoadModelFromPaths(
    EditorState& editor, language::lazy_string::LazyString paths);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_LOG_CONFIG_LOADER_H__
