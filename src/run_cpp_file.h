#ifndef __AFC_EDITOR_RUN_CPP_FILE_H__
#define __AFC_EDITOR_RUN_CPP_FILE_H__

#include <memory>
#include <string>

#include "command.h"
#include "src/futures/futures.h"

namespace afc::editor {

futures::Value<PossibleError> RunCppFileHandler(const std::wstring& input,
                                                EditorState& editor_state);
std::unique_ptr<Command> NewRunCppFileCommand(EditorState& editor_state);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_RUN_CPP_FILE_H__
