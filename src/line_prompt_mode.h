#ifndef __AFC_EDITOR_LINE_PROMPT_MODE_H__
#define __AFC_EDITOR_LINE_PROMPT_MODE_H__

#include <memory>

#include "command.h"
#include "editor.h"
#include "predictor.h"

namespace afc {
namespace editor {

using std::unique_ptr;

struct PromptOptions {
  // Text to show in the prompt.
  wstring prompt;

  // Optional. Name of the file with the history for this type of prompt.
  // Defaults to no history.
  wstring history_file;

  // Optional. Initial value for the prompt. Defaults to empty.
  wstring initial_value;

  // Function to run when the prompt receives the final input.
  std::function<void(const wstring& input, EditorState* editor)> handler;

  // Optional. Function to run when the prompt is cancelled (because ESCAPE was
  // pressed). If empty, handler will be run with an empty input.
  std::function<void(EditorState* editor)> cancel_handler;

  // Optional. Useful for automatic completion.
  Predictor predictor = EmptyPredictor;
};

void Prompt(EditorState* editor_state, PromptOptions options);

unique_ptr<Command> NewLinePromptCommand(wstring description,
                                         PromptOptions options);

}  // namespace editor
}  // namespace afc

#endif
