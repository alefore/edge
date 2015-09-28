#ifndef __AFC_EDITOR_LINE_PROMPT_MODE_H__
#define __AFC_EDITOR_LINE_PROMPT_MODE_H__

#include <memory>

#include "command.h"
#include "editor.h"
#include "predictor.h"

namespace afc {
namespace editor {

using std::unique_ptr;

typedef std::function<void(const wstring& input, EditorState* editor)>
    LinePromptHandler;

void Prompt(EditorState* editor_state,
            const wstring& prompt,
            const wstring& history_file,
            const wstring& initial_value,
            LinePromptHandler handler,
            Predictor predictor);

unique_ptr<Command> NewLinePromptCommand(
    const wstring& prompt, const wstring& history_file,
    const wstring& description, LinePromptHandler handler,
    Predictor predictor);

}  // namespace editor
}  // namespace afc

#endif
