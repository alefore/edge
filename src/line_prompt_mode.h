#ifndef __AFC_EDITOR_LINE_PROMPT_MODE_H__
#define __AFC_EDITOR_LINE_PROMPT_MODE_H__

#include <memory>

#include "command.h"
#include "editor.h"
#include "predictor.h"

namespace afc {
namespace editor {

using std::unique_ptr;

typedef std::function<void(const string& input, EditorState* editor)>
    LinePromptHandler;

void Prompt(EditorState* editor_state,
            const string& prompt,
            const string& history_file,
            const string& initial_value,
            LinePromptHandler handler,
            Predictor predictor);

unique_ptr<Command> NewLinePromptCommand(
    const string& prompt, const string& history_file, const string& description,
    LinePromptHandler handler, Predictor predictor);

}  // namespace editor
}  // namespace afc

#endif
