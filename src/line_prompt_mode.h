#ifndef __AFC_EDITOR_LINE_PROMPT_MODE_H__
#define __AFC_EDITOR_LINE_PROMPT_MODE_H__

#include <memory>

#include "src/command.h"
#include "src/editor.h"
#include "src/predictor.h"
#include "src/tokenize.h"

namespace afc {
namespace editor {

using std::unique_ptr;

struct PromptOptions {
  EditorState* editor_state = nullptr;

  // Text to show in the prompt.
  wstring prompt;

  std::wstring prompt_contents_type;

  // Optional. Name of the file with the history for this type of prompt.
  // Defaults to no history.
  wstring history_file;

  // Optional. Initial value for the prompt. Defaults to empty.
  wstring initial_value;

  // Run any time the text in the prompt changes.
  //
  // The prompt buffer is passed as an argument. To get the current text in the
  // prompt, use:
  //
  // auto line = buffer->LineAt(LineNumber(0));
  //
  // TODO(easy): Turn this into an options structure.
  std::function<futures::Value<bool>(const std::shared_ptr<OpenBuffer>&)>
      change_handler = [](const std::shared_ptr<OpenBuffer>&) {
        return futures::Past(true);  // Nothing.
      };

  // Function to run when the prompt receives the final input.
  std::function<futures::Value<bool>(const wstring& input, EditorState* editor)>
      handler;

  // Optional. Function to run when the prompt is cancelled (because ESCAPE was
  // pressed). If empty, handler will be run with an empty input.
  std::function<void(EditorState* editor)> cancel_handler;

  // Optional. Useful for automatic completion.
  Predictor predictor = EmptyPredictor;

  // Source buffers to give to the predictor. See
  // `PredictorInput::source_buffers`.
  std::vector<std::shared_ptr<OpenBuffer>> source_buffers;

  enum class Status { kEditor, kBuffer };
  Status status = Status::kEditor;
};

void Prompt(PromptOptions options);

unique_ptr<Command> NewLinePromptCommand(
    wstring description, std::function<PromptOptions(EditorState*)> options);

struct TokenAndModifiers {
  // The portion to colorize. The `value` field is ignored; instead, the
  // corresponding portion from the value in `prompt` will be used.
  Token token;
  // Set of modifiers to apply.
  LineModifierSet modifiers;
};

// status_buffer is the buffer with the contents of the prompt. tokens_future is
// received as a future so that we can detect if the prompt input changes
// between the time when `ColorizePrompt` is executed and the time when the
// tokens become available.
futures::Value<bool> ColorizePrompt(
    std::shared_ptr<OpenBuffer> status_buffer,
    futures::Value<std::vector<TokenAndModifiers>> tokens_future);

}  // namespace editor
}  // namespace afc

#endif
