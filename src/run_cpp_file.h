#ifndef __AFC_EDITOR_RUN_CPP_FILE_H__
#define __AFC_EDITOR_RUN_CPP_FILE_H__

#include <memory>
#include <string>

#include "command.h"

namespace afc {
namespace editor {

void RunCppFileHandler(const std::wstring& input, EditorState* editor_state);
std::unique_ptr<Command> NewRunCppFileCommand();

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_RUN_CPP_FILE_H__
