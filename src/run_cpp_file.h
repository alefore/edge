#ifndef __AFC_EDITOR_RUN_CPP_FILE_H__
#define __AFC_EDITOR_RUN_CPP_FILE_H__

#include <memory>
#include <string>

#include "src/futures/futures.h"
#include "src/language/safe_types.h"
#include "src/language/value_or_error.h"

namespace afc::editor {
class EditorState;
class Command;
futures::Value<language::PossibleError> RunCppFileHandler(
    const std::wstring& input, EditorState& editor_state);

language::NonNull<std::unique_ptr<Command>> NewRunCppFileCommand(
    EditorState& editor_state);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_RUN_CPP_FILE_H__
